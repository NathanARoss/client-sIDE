const UTF8Decoder = new TextDecoder();
const UTF8Encoder = new TextEncoder();

let buffer = "";
function bufferedPuts(chars, stdout) {
    //wait for newlines before sending strings to stdout to match native environments.
    buffer += chars;
    
    let index = buffer.lastIndexOf('\n');
    if (index !== -1) {
        const lines = buffer.substr(0, index + 1);
        stdout(lines);
        buffer = buffer.substr(index + 1);
    }
}

//TODO call at the end of every function that prints
function flushStdout(stdout) {
    if (buffer) {
        stdout(buffer);
    }
}

function createImportObject(providedImports) {
    //this is assumed to be assigned externally after this function returns
    this.memoryUint8 = null;

    //in the future, all functions will have their input and output pre-processed
    let {stdout, ...env} = providedImports || {};
    stdout = stdout || console.log;

    //clang only looks for extern C functions from "env", so all imported
    //functions must be in that namespace
    this.env = env;

    //for instance, here puts receives from Wasm an address and number
    //of bytes, but the calling code receives a String object instead
    this.env.puts = (address, size) => {
        const data = this.memoryUint8.subarray(address, address + size);
        const message = UTF8Decoder.decode(data);
        bufferedPuts(message, stdout);
    };

    //since JS has no character type, just call puts with a String of length 1
    this.env.put = function(char) {
        if (char < 0) {
            //clang encodes some characters as small negative numbers
            char += 128;
        }
        const message = String.fromCharCode(char);
        bufferedPuts(message, stdout);
    };

    this.env.putnum = function(num) {
        bufferedPuts(String(num), stdout);
    }

    this.env.putu32 = function(u32Num) {
        if (u32Num < 0) {
            u32Num += 1 ** 32;
        }
        bufferedPuts(String(u32Num), stdout);
    };

    //clang is stuborn about extern C, so each type requires its own import
    this.env.putf32 = env.putnum;
    this.env.putf64 = env.putnum;

    this.env = Object.assign(this.env, Math);
}

export default function getCompiler(language, customImports) {
    const imports = new createImportObject(customImports);

    //only supported language for now
    const wasmFile = "cpp.wasm";

    return new Promise((resolve, reject) => {        
        fetch(wasmFile).then(response =>
            response.arrayBuffer()
        ).then(bytes =>
            WebAssembly.instantiate(bytes, imports)
        ).then(results => {
            const exports = results.instance.exports;
            imports.memoryUint8 = new Uint8Array(exports.memory.buffer);

            //this object exposes the two public functions but not any internal details
            const compiler = {
                compileToWasmBinary(sourceCode) {
                    const strAsUTF8 = UTF8Encoder.encode(sourceCode);
                
                    imports.memoryUint8.set(strAsUTF8, exports.__heap_base);
                
                    const addrAndSize = exports.getWasmFromCpp(exports.__heap_base, strAsUTF8.length);
                    const addr = addrAndSize >>> 16;
                    const size = addrAndSize & 0xFFFF;
                
                    return imports.memoryUint8.subarray(addr, addr + size);
                },
                compile(sourceCode, customImports) {
                    return new Promise((resolve, reject) => {    
                        const bytes = this.compileToWasmBinary(sourceCode);
                        const imports = new createImportObject(customImports);
                    
                        WebAssembly.instantiate(bytes, imports)
                        .then((results) => {
                            const runtimeExports = results.instance.exports;
                            if (runtimeExports.memory) {
                                imports.memoryUint8 = new Uint8Array(runtimeExports.memory.buffer);
                            }
                
                            resolve(runtimeExports);
                        }).catch((error) => {
                            reject(error);
                        });
                    });
                }
            }
        
            resolve(compiler);
        }).catch(error => {
            reject(error);
        });
    });
}