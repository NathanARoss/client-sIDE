const canvas = document.getElementById("playground");
let minDim = 1;

const sampleProgram = `\
#include <iostream>
#include <canvas>

/* In this early version of Toy++ and ncc, only
basic syntax is supported. Global variables must
not have initial values. Only one operation per
line (and no combined operators). No loops.
No defining your own functions.

However, arbitrary global and local variables
(of float type) are supported. State can be
saved between frames using global variables as
demonstrated.

If statements are supported, but not elses or
compound boolean expressions.

Print statements are supported, but only for
single string literals.

This is a Minimum Viable Product which I will
work on over winter break. */

float vx;
float vy;
float x;
float y;

float elasticity;

void start() {
    x = -1.0f;
    y = 0.0f;
    vx = 0.015f;
    vy = 0.02f;
    elasticity = -0.8f;
}

void loop(float elapsedSeconds, float deltaSeconds) {
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

    window.onresize = function () {
        editor.layout();
        var scale = window.devicePixelRatio;
        canvas.width = window.innerWidth * scale;
        canvas.height = window.innerHeight * scale;

        minDim = Math.min(canvas.width, canvas.height);
        console.log("resizing to: ", canvas.width, canvas.height);
    }
    window.onresize();
}