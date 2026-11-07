// Copyright (C) 2026, VisionJK contributors
//
// This file is part of the OpenJK source code. OpenJK is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License version 2 as published by the Free Software Foundation.
//
// Run by the build after a module for the browser is linked (cmake/Modules/WasmModule.cmake):
//   node CheckWasmImports.js <engine.wasm> <module.wasm>
//
// A side module links whatever it does not have itself against the main module when it is loaded, and a function
// that is not there only shows when it is called. On the desktop the linker says so at build time (-z defs).
// This says it here: every function a module imports has to be one the engine exports, one the engine itself gets
// from the JavaScript side of Emscripten (exit is one), or one of WebGL, which is on that side too.
const fs = require( 'fs' );
const [enginePath, modulePath] = process.argv.slice( 2 );
const engine = new WebAssembly.Module( fs.readFileSync( enginePath ) );
const known = new Set( WebAssembly.Module.exports( engine ).concat( WebAssembly.Module.imports( engine ) ).map( ( e ) => e.name ) );
const missing = WebAssembly.Module.imports( new WebAssembly.Module( fs.readFileSync( modulePath ) ) )
	.filter( ( i ) => i.kind === 'function' && !known.has( i.name ) && !/^(gl[A-Z]|emscripten_gl|invoke_)/.test( i.name ) )
	.map( ( i ) => i.name );

if ( missing.length ) {
	console.error( `${modulePath}: not in the engine and not WebGL: ${missing.join( ' ' )}` );
	process.exit( 1 );
}
