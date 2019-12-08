const canvas = document.getElementById("playground");
const consoleOutput = document.getElementById("console");
const playBttn = document.getElementById("play-bttn");
const pauseBttn = document.getElementById("pausebutton");
const UTF8Decoder = new TextDecoder("utf-8");
const UTF8Encoder = new TextEncoder("utf-8");

var ctx = canvas.getContext('2d');
let minDim = 1;
let maxDim = 1;
let secondsElapsedBeforePause = 0;
let activeWasmModule;
let compileTimestamp;
let prevTimeStamp;

pauseBttn.onclick = function() {
    if (activeWasmModule && secondsElapsedBeforePause === 0) {
        secondsElapsedBeforePause = performance.now() / 1000 - compileTimestamp;
    } else {
        compileTimestamp = performance.now() / 1000 - secondsElapsedBeforePause;
        prevTimeStamp = compileTimestamp;
        requestAnimationFrame(draw);
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
            printToConsole(`drawCircle(${x}, ${y}, ${r})\n`)

            //normalize coordinates so a (-1,-1) to (1,1) box is contained
            //and centered on screen.
            x = (x * minDim + canvas.width) / 2;
            y = (y * minDim + canvas.height) / 2;
            r = r / 2 * minDim;

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

fetch('ncc.wasm').then(response =>
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
        } else {
            compileAndRun(newBytes);
        }
    }

    playBttn.onclick = compileClick;
    playBttn.oncontextmenu = compileClick;
});

function compileAndRun(userGeneratedWasmBytes) {
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
        requestAnimationFrame(draw);
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
    console.log(secondsElapsedBeforePause)
    if (!activeWasmModule || secondsElapsedBeforePause === 0) {
        return;
    }

    const seconds = timestamp / 1000 - compileTimestamp;
    const delta = seconds - prevTimeStamp;
    prevTimeStamp = seconds;

    if (activeWasmModule.loop) {
        activeWasmModule.loop(seconds, delta);
    }

    requestAnimationFrame(draw);
}