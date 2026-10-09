/*
===========================================================================
Copyright (C) 2026, VisionJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// What the engine needs from the page, see sys_web.h. Written against Emscripten 6.0.12.
//
// The loader of the modules: Emscripten's dlclose() never unloads and its dlopen() hands back the instance it
// has, with the statics as they were left. The engine counts on fresh statics with every load (a map change,
// vid_restart), so each load here makes a new instance from the compiled module the page keeps in
// Module.vjkModules, with a data block of its own that is freed again when the module is unloaded.
addToLibrary( {
	$vjkLibs: { next: 1, byHandle: {} },

	// loadWebAssemblyModule() of libdylink.js is in every main module. It is not named as a dependency: the
	// linker would then ask for __heap_base on behalf of this function before it has made that symbol.
	Sys_WebLoadLibrary__deps: ['$vjkLibs', '$UTF8ToString', 'calloc', 'free'],
	Sys_WebLoadLibrary: ( namePtr ) => {
		var path = UTF8ToString( namePtr );
		var name = path.substring( path.lastIndexOf( '/' ) + 1 );
		var module = Module['vjkModules'] && Module['vjkModules'][name];

		if ( !module ) {
			return 0;
		}
		// loadWebAssemblyModule() notes where it put the data of the module in a struct dso
		var dso = _calloc( 1, {{{ C_STRUCTS.dso.__size__ }}} );
		var exports;
		try {
			exports = loadWebAssemblyModule( module, { loadAsync: false, global: false, nodelete: false }, name, {}, dso );
		} catch ( e ) {
			err( `${name}: ${e}` );
			_free( dso );
			return 0;
		}
		var handle = vjkLibs.next++;
		vjkLibs.byHandle[handle] = { name, exports, dso, funcs: [] };
		return handle;
	},

	Sys_WebLoadFunction__deps: ['$vjkLibs', '$UTF8ToString', '$addFunction'],
	Sys_WebLoadFunction: ( handle, namePtr ) => {
		var lib = vjkLibs.byHandle[handle];
		var func = lib && lib.exports[UTF8ToString( namePtr )];

		if ( typeof func != 'function' ) {
			return 0;
		}
		var ptr = addFunction( func );
		lib.funcs.push( ptr );
		return ptr;
	},

	Sys_WebUnloadLibrary__deps: ['$vjkLibs', '$removeFunction', 'free'],
	Sys_WebUnloadLibrary: ( handle ) => {
		var lib = vjkLibs.byHandle[handle];

		if ( !lib ) {
			return;
		}
		lib.funcs.forEach( removeFunction );
		var mem = {{{ makeGetValue( 'lib.dso', C_STRUCTS.dso.mem_addr, '*' ) }}};
		// The block came from calloc and was then aligned for the module. Both are the same address as long as
		// the module asks for no more than the 16 bytes malloc aligns to anyway, otherwise the block stays.
		if ( mem && ( mem % 16 ) == 0 ) {
			_free( mem );
		}
		_free( lib.dso );
		delete vjkLibs.byHandle[handle];
	},

	Sys_WebShowError__deps: ['$UTF8ToString'],
	Sys_WebShowError: ( textPtr ) => {
		var text = UTF8ToString( textPtr );

		if ( Module['vjkError'] ) {
			Module['vjkError']( text );
		} else {
			err( text );
		}
	},
} );
