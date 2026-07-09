(() => {
"use strict";

const {
  MAX_ROM_SIZE,
  MIN_ROM_SIZE,
  RomInspectionError,
  formatBytes,
  formatExactBytes,
  formatHex,
  inspectRom,
  makeTrimmedFilename,
} = globalThis.RomTrimmer;

const byId = (id) => document.getElementById(id);

const elements = {
  dropSurface: byId("drop-surface"),
  input: byId("rom-input"),
  idle: byId("idle-state"),
  processing: byId("processing-state"),
  result: byId("result-state"),
  error: byId("error-state"),
  processingFilename: byId("processing-filename"),
  resultIcon: byId("result-icon"),
  resultKicker: byId("result-kicker"),
  resultTitle: byId("result-title"),
  resultFilename: byId("result-filename"),
  resultRomTitle: byId("result-rom-title"),
  resultGameCode: byId("result-game-code"),
  resultSummary: byId("result-summary"),
  capacityPercent: byId("capacity-percent"),
  capacityMeter: byId("capacity-meter"),
  capacityFill: byId("capacity-fill"),
  originalSize: byId("original-size"),
  trimmedSize: byId("trimmed-size"),
  savedSize: byId("saved-size"),
  technicalDetails: byId("technical-details"),
  detailPaddingByte: byId("detail-padding-byte"),
  detailPaddingSize: byId("detail-padding-size"),
  detailOutputEnd: byId("detail-output-end"),
  detailHeader: byId("detail-header"),
  downloadButton: byId("download-button"),
  chooseAnotherButton: byId("choose-another-button"),
  downloadStatus: byId("download-status"),
  errorTitle: byId("error-title"),
  errorMessage: byId("error-message"),
  overrideButton: byId("override-button"),
  retryButton: byId("retry-button"),
  liveStatus: byId("live-status"),
};

const panels = [elements.idle, elements.processing, elements.result, elements.error];

let currentFile = null;
let currentBytes = null;
let currentAnalysis = null;
let inspectionSequence = 0;

function showPanel(panel) {
  for (const candidate of panels) candidate.hidden = candidate !== panel;
}

function announce(message) {
  elements.liveStatus.textContent = "";
  window.requestAnimationFrame(() => {
    elements.liveStatus.textContent = message;
  });
}

function focusAfterRender(element) {
  window.requestAnimationFrame(() => element.focus({ preventScroll: true }));
}

function preflightFile(file) {
  if (file.size < MIN_ROM_SIZE) {
    throw new RomInspectionError("too-small", "The file is smaller than a GBA header.");
  }
  if (file.size > MAX_ROM_SIZE) {
    throw new RomInspectionError("too-large", "The file is larger than the GBA ROM window.");
  }
}

function errorCopy(error) {
  switch (error?.code) {
    case "multiple-files":
      return {
        title: "Choose one ROM at a time",
        message: "Only one output can be reviewed and downloaded at a time.",
      };
    case "archive":
      return {
        title: "Unzip this file first",
        message: "This looks like an archive. Unpack it, then choose the .gba file inside.",
      };
    case "too-small":
      return {
        title: "Not a complete GBA ROM",
        message: "This file is too small to contain the full GBA cartridge header. Nothing was changed.",
      };
    case "too-large":
      return {
        title: "This file is over 32 MiB",
        message: "Standard GBA ROMs fit within a 32 MiB address window. Check that you selected the right file.",
      };
    case "nonstandard-header":
      return {
        title: "Non-standard GBA header",
        message:
          "The logo signature, fixed byte, or header checksum did not match. Nothing was changed. If you trust this ROM or homebrew build, you can inspect its tail anyway.",
        canOverride: true,
      };
    default:
      return {
        title: "We could not read this file",
        message: "Try closing other tabs, then choose the ROM again. Nothing was changed.",
      };
  }
}

function renderError(error) {
  const copy = errorCopy(error);
  elements.errorTitle.textContent = copy.title;
  elements.errorMessage.textContent = copy.message;
  elements.overrideButton.hidden = !copy.canOverride;
  showPanel(elements.error);
  announce(copy.title);
  focusAfterRender(elements.errorTitle);
}

function renderResult(analysis) {
  const isReady = analysis.status === "ready";
  const retainedPercent = (analysis.trimmedSize / analysis.originalSize) * 100;
  const savedPercent = (analysis.savedSize / analysis.originalSize) * 100;
  const paddingLabel =
    analysis.paddingByte === null
      ? "—"
      : `0x${analysis.paddingByte.toString(16).toUpperCase().padStart(2, "0")}`;

  elements.result.classList.toggle("is-compact", !isReady);
  elements.resultIcon.textContent = isReady ? "✓" : "=";
  elements.resultKicker.textContent = isReady ? "Blank tail found" : "No large blank tail";
  elements.resultTitle.textContent = isReady ? "Ready to trim" : "Already compact";
  elements.resultFilename.textContent = currentFile.name;
  elements.resultRomTitle.textContent = analysis.title;
  elements.resultGameCode.textContent = analysis.gameCode;

  if (isReady) {
    const verificationNote = analysis.verified
      ? ""
      : " The header is non-standard, so review this output especially carefully.";
    elements.resultSummary.textContent = `${formatBytes(analysis.savedSize)} (${savedPercent.toFixed(
      1,
    )}%) of trailing ${paddingLabel} fill can be removed.${verificationNote}`;
  } else if (analysis.reason === "padding-below-threshold") {
    elements.resultSummary.textContent = `The final ${paddingLabel} run is smaller than the conservative 4 KiB threshold, so TrimPak left it in place.`;
  } else {
    elements.resultSummary.textContent =
      "No removable 0xFF or 0x00 tail was found. The file has not been changed.";
  }

  elements.capacityPercent.textContent = `${retainedPercent.toFixed(1)}% retained`;
  elements.capacityMeter.setAttribute(
    "aria-label",
    `ROM after trimming: ${retainedPercent.toFixed(1)} percent of the original size.`,
  );
  elements.capacityFill.style.width = "0%";
  window.requestAnimationFrame(() => {
    elements.capacityFill.style.width = `${retainedPercent}%`;
  });

  elements.originalSize.textContent = formatBytes(analysis.originalSize);
  elements.trimmedSize.textContent = formatBytes(analysis.trimmedSize);
  elements.savedSize.textContent = formatBytes(analysis.savedSize);
  elements.detailPaddingByte.textContent = paddingLabel;
  elements.detailPaddingSize.textContent = formatExactBytes(analysis.uniformPaddingBytes);
  elements.detailOutputEnd.textContent = formatHex(Math.max(0, analysis.trimmedSize - 1));
  elements.detailHeader.textContent = analysis.verified ? "Logo + fixed byte + checksum" : "Bypassed by user";
  elements.downloadButton.hidden = !isReady;
  elements.downloadStatus.textContent = "";
  elements.technicalDetails.open = false;

  showPanel(elements.result);
  announce(isReady ? `Ready to trim ${currentFile.name}.` : `${currentFile.name} is already compact.`);
  focusAfterRender(elements.resultTitle);
}

async function inspectFile(file, allowUnverified = false) {
  const sequence = ++inspectionSequence;
  currentFile = file;
  currentAnalysis = null;

  elements.processingFilename.textContent = file.name;
  showPanel(elements.processing);
  announce(`Inspecting ${file.name} locally.`);

  try {
    preflightFile(file);
    await new Promise((resolve) => window.requestAnimationFrame(resolve));

    if (!currentBytes) {
      const buffer = await file.arrayBuffer();
      if (sequence !== inspectionSequence) return;
      currentBytes = new Uint8Array(buffer);
    }

    currentAnalysis = inspectRom(currentBytes, { allowUnverified });
    if (sequence !== inspectionSequence) return;
    renderResult(currentAnalysis);

    // The File object is enough to create the exact output slice; release the
    // in-memory copy after a successful inspection.
    currentBytes = null;
  } catch (error) {
    if (sequence !== inspectionSequence) return;
    renderError(error);
  }
}

function resetWorkbench(openPicker = false) {
  inspectionSequence += 1;
  currentFile = null;
  currentBytes = null;
  currentAnalysis = null;
  elements.input.value = "";
  elements.dropSurface.classList.remove("is-dragging");
  elements.downloadStatus.textContent = "";
  showPanel(elements.idle);
  announce("Ready for a GBA ROM.");

  if (openPicker) {
    window.requestAnimationFrame(() => elements.input.click());
  }
}

function handleFiles(files) {
  const selected = Array.from(files || []);
  elements.input.value = "";

  if (selected.length === 0) return;
  if (selected.length > 1) {
    currentFile = null;
    currentBytes = null;
    renderError(new RomInspectionError("multiple-files", "Choose one file."));
    return;
  }

  currentBytes = null;
  inspectFile(selected[0]);
}

function downloadTrimmedRom() {
  if (!currentFile || !currentAnalysis || currentAnalysis.status !== "ready") return;

  const output = currentFile.slice(0, currentAnalysis.trimmedSize, "application/octet-stream");
  const url = URL.createObjectURL(output);
  const link = document.createElement("a");
  link.href = url;
  link.download = makeTrimmedFilename(currentFile.name);
  link.hidden = true;
  document.body.append(link);
  link.click();
  link.remove();
  window.setTimeout(() => URL.revokeObjectURL(url), 30_000);

  elements.downloadStatus.textContent = "Trimmed ROM downloaded. Your original is unchanged.";
  announce(`${link.download} downloaded.`);
}

elements.input.addEventListener("change", (event) => handleFiles(event.target.files));
elements.downloadButton.addEventListener("click", downloadTrimmedRom);
elements.chooseAnotherButton.addEventListener("click", () => resetWorkbench(true));
elements.retryButton.addEventListener("click", () => resetWorkbench(true));
elements.overrideButton.addEventListener("click", () => {
  if (currentFile) inspectFile(currentFile, true);
});

for (const eventName of ["dragenter", "dragover"]) {
  elements.dropSurface.addEventListener(eventName, (event) => {
    event.preventDefault();
    event.dataTransfer.dropEffect = "copy";
    elements.dropSurface.classList.add("is-dragging");
  });
}

for (const eventName of ["dragleave", "dragend"]) {
  elements.dropSurface.addEventListener(eventName, (event) => {
    event.preventDefault();
    if (eventName === "dragleave" && elements.dropSurface.contains(event.relatedTarget)) return;
    elements.dropSurface.classList.remove("is-dragging");
  });
}

elements.dropSurface.addEventListener("drop", (event) => {
  event.preventDefault();
  elements.dropSurface.classList.remove("is-dragging");
  handleFiles(event.dataTransfer.files);
});

document.addEventListener("dragover", (event) => event.preventDefault());
document.addEventListener("drop", (event) => {
  if (!elements.dropSurface.contains(event.target)) event.preventDefault();
});
})();
