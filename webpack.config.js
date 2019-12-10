const MonacoWebpackPlugin = require('monaco-editor-webpack-plugin');
const path = require('path');

module.exports = {
    node: {
        fs: 'empty'
    },
    mode: 'production',
    entry: './index.js',
    output: {
        path: path.resolve(__dirname, 'public'),
        filename: "monaco.bundle.js",
    },
    module: {
        rules: [{
            test: /\.css$/,
            use: ['style-loader', 'css-loader']
        }]
    },
    plugins: [
        new MonacoWebpackPlugin({
            languages: ["cpp"]
        })
    ]
};