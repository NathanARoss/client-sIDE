import getDisassembly from "https://nathanross.me/small-wasm-disassembler/disassembler.min.mjs"; //TODO load this dynamically
import getCompiler from "../compiler.mjs";

const consoleOutput = document.getElementById("console");
const playBttn = document.getElementById("play-bttn");
const pauseBttn = document.getElementById("pause-bttn");
const backBttn = document.getElementById("back-bttn");
const disassembleBttn = document.getElementById("disassemble-bttn");

const ctx = canvas.getContext('2d');
let secondsElapsedBeforePause = 0;
let activeWasmModule;
let compileTimestamp;
let prevTimeStamp;
let frameRequestId;

function playClicked() {
    compiler.compile(
        editor.getValue(),
        {
            stdout: printToConsole,
            drawCircle: drawCircle
        }
    ).then(executeProgram);
};

function disassembleClicked(event) {
    const bytes = compiler.compileToWasmBinary(editor.getValue());
    
    if (event.type == "contextmenu") {
        saveFile("user.wasm", bytes);
    } else {
        printToConsole("\n" + getDisassembly(bytes, 9) + "\n");
        document.body.className = "console-mode";
        
        if (!activeWasmModule) {
            document.body.className += " no-active-module";
        }
    }
}

pauseBttn.onclick = function() {
    if (activeWasmModule && secondsElapsedBeforePause === 0) {
        secondsElapsedBeforePause = performance.now() / 1000 - compileTimestamp;

        if (frameRequestId !== undefined) {
            cancelAnimationFrame(frameRequestId);
            frameRequestId = undefined;
        }
    } else {
        compileTimestamp = performance.now() / 1000 - secondsElapsedBeforePause;
        prevTimeStamp = compileTimestamp;

        if (frameRequestId === undefined) {
            requestAnimationFrame(draw);
        }
        secondsElapsedBeforePause = 0;
    }

    pauseBttn.classList.toggle("active", secondsElapsedBeforePause !== 0);
}

backBttn.onclick = function() {
    activeWasmModule = undefined;

    if (frameRequestId !== undefined) {
        cancelAnimationFrame(frameRequestId);
        frameRequestId = undefined;
    }

    secondsElapsedBeforePause = 0;
    pauseBttn.classList.remove("active");
    document.body.className = "edit-mode";
}

function printToConsole(value) {
    if (consoleOutput.childNodes.length == 0 || consoleOutput.lastChild.nodeValue.length > 512) {
        const textNode = document.createTextNode(value);
        consoleOutput.appendChild(textNode);
    } else {
        consoleOutput.lastChild.nodeValue += value;
    }
}

function drawCircle(x, y, r) {
    // printToConsole(`drawCircle(${x}, ${y}, ${r})\n`)

    //normalize coordinates so a (-1,-1) to (1,1) box is contained
    //and centered on screen.
    x = (x * minDim + canvas.width) / 2;
    y = (canvas.height - y * minDim) / 2;
    r = r / 2 * minDim;

    const seconds = performance.now() / 1000 - compileTimestamp;
    const progress = seconds / 4;
    const hue = (progress - Math.floor(progress)) * 360;
    ctx.fillStyle = "hsl(" + hue + ", 100%, 50%)"
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI*2);
    ctx.fill();
}

getCompiler("cpp", {
    stdout: printToConsole
}).then(compilerInstance => {
    compiler = compilerInstance;

    //monaco finished loading first
    if (editor) {
        activateMenu();
    }
});

function activateMenu() {
    playBttn.onclick = playClicked;
    disassembleBttn.onclick = disassembleClicked;
    disassembleBttn.oncontextmenu = disassembleClicked;
    printToConsole("Loaded. An internet connection is no longer required.\n\n");
}

function executeProgram(runtime) {
    //clear the console before running main
    consoleOutput.innerHTML = "";

    if (runtime.main) {
        runtime.main();
    }

    activeWasmModule = runtime;
    compileTimestamp = performance.now() / 1000;
    prevTimeStamp = compileTimestamp;
    secondsElapsedBeforePause = 0;
    pauseBttn.classList.remove("active");
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    if (frameRequestId === undefined) {
        frameRequestId = requestAnimationFrame(draw);
    }
    
    document.body.className = "canvas-mode";
}

function saveFile(filename, content) {
    var a = document.createElement('a');
    a.href = window.URL.createObjectURL(new File([content], filename));
    a.download = filename;

    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);

    window.URL.revokeObjectURL(a.href);
}

function draw(timestamp) {
    const elapsedSeconds = timestamp / 1000 - compileTimestamp;
    const delta = elapsedSeconds - prevTimeStamp;
    prevTimeStamp = elapsedSeconds;

    if (activeWasmModule.update) {
        activeWasmModule.update(elapsedSeconds, delta);
    }

    frameRequestId = requestAnimationFrame(draw);
}