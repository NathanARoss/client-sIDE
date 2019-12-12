import getDisassembly from "https://nathanross.me/small-wasm-disassembler/disassembler.min.mjs"; //TODO load this dynamically

const consoleOutput = document.getElementById("console");
const playBttn = document.getElementById("play-bttn");
const pauseBttn = document.getElementById("pause-bttn");
const disassembleBttn = document.getElementById("disassemble-bttn");
const UTF8Decoder = new TextDecoder("utf-8");

const ctx = canvas.getContext('2d');
let secondsElapsedBeforePause = 0;
let activeWasmModule;
let compileTimestamp;
let prevTimeStamp;
let frameRequestId;

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

function printToConsole(value) {
    if (consoleOutput.childNodes.length == 0 || consoleOutput.lastChild.nodeValue.length > 512) {
        const textNode = document.createTextNode(value);
        consoleOutput.appendChild(textNode);
    } else {
        consoleOutput.lastChild.nodeValue += value;
    }
}

function createRuntime() {
    this.memoryUbytes;
    const self = this;

    this.env = {
        puts(address, size) {
            const data = self.memoryUbytes.subarray(address, address + size);
            const message = UTF8Decoder.decode(data);
            printToConsole(message);
        },

        put(char) {
            if (char < 0) {
                //clang is producing negative characters.
                //temporary fix
                char += 128;
            }
            const message = String.fromCharCode(char);
            printToConsole(message);
        },

        putbool(value) {
            const message = String(!!value);
            printToConsole(message);
        },

        puti32(num) {
            const message = String(num);
            printToConsole(message);
        },

        putf32(num) {
            const message = String(num);
            printToConsole(message);
        },

        putu32(u32Num) {
            if (u32Num < 0) {
                u32Num += 1 ** 32;
            }
            const message = String(u32Num);
            printToConsole(message);
        },

        drawCircle(x, y, r) {
            // printToConsole(`drawCircle(${x}, ${y}, ${r})\n`)

            //normalize coordinates so a (-1,-1) to (1,1) box is contained
            //and centered on screen.
            x = (x * minDim + canvas.width) / 2;
            y = (canvas.height - y * minDim) / 2;
            r = r / 2 * minDim;

            const hue = ((performance.now() / 2000) - Math.floor(performance.now() / 2000)) * 360;
            ctx.fillStyle = "hsl(" + hue + ", 100%, 50%)"
            ctx.beginPath();
            ctx.arc(x, y, r, 0, Math.PI*2);
            ctx.fill();
        }
    };

    this.Math = Math;
}

const compilerImports = new createRuntime();
const runtimeImports = new createRuntime();


// WebAssembly.instantiateStreaming(fetch('ncc.wasm'), wasmImports)
//     .then(results => {
//         // Do something with the results!
//         console.log(results);
//     });

fetch('public/ncc.wasm').then(response =>
    response.arrayBuffer()
).then(bytes =>
    WebAssembly.instantiate(bytes, compilerImports)
).then(results => {
    const compilerExports = results.instance.exports;
    compilerImports.memoryUbytes = new Uint8Array(compilerExports.memory.buffer);

    function compileClick(event) {
        if (!editor) {
            //monaco hasn't loaded yet
            return;
        }

        const encoder = new TextEncoder()
        const strAsUTF8 = encoder.encode(editor.getValue())

        compilerImports.memoryUbytes.set(strAsUTF8, compilerExports.__heap_base);

        const addrAndSize = compilerExports.getWasmFromCpp(compilerExports.__heap_base, strAsUTF8.length);
        const addr = addrAndSize >>> 16;
        const size = addrAndSize & 0xFFFF;

        const newBytes = compilerImports.memoryUbytes.subarray(addr, addr + size);

        if (event.type == "contextmenu") {
            saveFile("user.wasm", newBytes);
        } else if (event.currentTarget === disassembleBttn) {
            printToConsole("\n" + getDisassembly(newBytes, 9) + "\n");
        } else {
            compileAndRun(newBytes);
        }
    }

    playBttn.onclick = compileClick;
    disassembleBttn.onclick = compileClick;
    disassembleBttn.oncontextmenu = compileClick;
    printToConsole("Loaded. An internet connection is no longer required.\n\n");
});

function compileAndRun(userGeneratedWasmBytes) {
    //clear the console
    consoleOutput.innerHTML = "";

    WebAssembly.instantiate(userGeneratedWasmBytes, runtimeImports)
    .then((results) => {
        const runtimeExports = results.instance.exports;
        if (runtimeExports.memory) {
            runtimeExports.memoryUbytes = new Uint8Array(runtimeExports.memory.buffer);
        }

        if (runtimeExports.start) {
            runtimeExports.start();
        }

        activeWasmModule = runtimeExports;
        compileTimestamp = performance.now() / 1000;
        prevTimeStamp = compileTimestamp;
        secondsElapsedBeforePause = 0;
        pauseBttn.classList.remove("active");
        ctx.clearRect(0, 0, canvas.width, canvas.height);

        if (frameRequestId === undefined) {
            frameRequestId = requestAnimationFrame(draw);
        }
    });
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

    if (activeWasmModule.loop) {
        activeWasmModule.loop(elapsedSeconds, delta);
    }

    frameRequestId = requestAnimationFrame(draw);
}