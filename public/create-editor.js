const canvas = document.getElementById("playground");
let minDim = 1;
let compiler;

const sampleProgram = `\
#include <iostream>
#include <canvas>

float vx;
float vy;
float x;
float y;

float elasticity;

void main() {
    x = -1.0f;
    y = 0.0f;
    vx = 0.015f;
    vy = 0.02f;
    elasticity = -0.8f;
}

void update(float secondsSinceStart, float secondsSincePrevFrame) {
    x = x + vx;
    y = y + vy;
    vy = vy - 0.0005f;

    if (y < -1.0f) {
        vy = vy * elasticity;
        y = -1;

        if (vy < 0.005f) {
            vy = vy + 0.035f;
            vx = x * -0.05f;
            std::cout << "Pop!\\n";
        }

        std::cout << "Boing!\\n";
    }

    if (x < -1) {
        vx = vx * elasticity;
        x = -1;
    }

    if (x > 1) {
        vx = vx * elasticity;
        x = 1;
    }

    drawCircle(x, y, 0.05);
}`;

//wait for the page to load since Monaco's JS won't be ready until then
let editor;

window.onload = function () {
    editor = monaco.editor.create(
        document.getElementById('monaco-editor-window'), {
            value: sampleProgram,
            language: 'cpp',
            theme: 'vs-dark',
            minimap: {
                enabled: false
            },
            scrollBeyondLastLine: false,
        }
    );

    editor.addAction({
        // An unique identifier of the contributed action.
        id: 'duplicate-selection-id',

        // A label of the action that will be presented to the user.
        label: 'Duplicate Selection',

        // An optional array of keybindings for the action.
        keybindings: [
            monaco.KeyMod.CtrlCmd | monaco.KeyCode.KEY_D,
            // chord
            // monaco.KeyMod.chord(monaco.KeyMod.CtrlCmd | monaco.KeyCode.KEY_K, monaco.KeyMod.CtrlCmd | monaco.KeyCode.KEY_M)
        ],

        // A precondition for this action.
        precondition: null,

        // A rule to evaluate on top of the precondition in order to dispatch the keybindings.
        keybindingContext: null,

        contextMenuGroupId: '9_cutcopypaste',

        contextMenuOrder: 4,

        // Method that will be executed when the action is triggered.
        // @param editor The editor instance is passed in as a convinience
        run: function (editor) {
            // editor.trigger('keyboard', 'type', { text: "test" });

            let currentSelection = editor.getSelection();

            let {
                lineNumber,
                column
            } = currentSelection.getEndPosition();
            let selectedText;
            let insertColumn = column;

            // pressign CTRL-D without selecting text should duplicate the line the cursor is on
            if (currentSelection.isEmpty()) {
                selectedText = editor.getModel().getLineContent(lineNumber) + '\n';
                ++lineNumber;
                insertColumn = 1;
            } else {
                selectedText = editor.getModel().getValueInRange(currentSelection);
            }

            editor.executeEdits("", [{
                range: {
                    startLineNumber: lineNumber,
                    startColumn: insertColumn,
                    endLineNumber: lineNumber,
                    endColumn: insertColumn
                },
                text: selectedText
            }]);

            const newSelection = editor.getSelection().setStartPosition(lineNumber, column);
            editor.setSelection(newSelection);

            return null;
        }
    });

    //the compiler finished loading first
    if (compiler) {
        activateMenu();
    }

    window.onresize = function () {
        editor.layout();
        var scale = window.devicePixelRatio;
        canvas.width = window.innerWidth * scale;
        canvas.height = window.innerHeight * scale;

        minDim = Math.min(canvas.width, canvas.height);
        // console.log("resizing to: ", canvas.width, canvas.height);
    }
    window.onresize();
}