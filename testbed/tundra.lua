require "tundra.native"
local native = require('tundra.native')

local macosx = {
	Env = {
		QT5 = native.getenv("QT5"),
		CCOPTS = {
			"-Wall",
			"-I.", "-DMACOSX", "-Weverything", "-Wno-missing-prototypes",
			{ "-O0", "-g"; Config = "*-*-debug" },
			{ "-O3", "-g"; Config = "*-*-release" },
		},

		CXXOPTS = {
			"-Werror", "-I.", "-DMACOSX", "-Weverything", 
			{ "-O0", "-g"; Config = "*-*-debug" },
			{ "-O3", "-g"; Config = "*-*-release" },
		},
	},

	Frameworks = { "Cocoa" },
}

local win32 = {
	Env = {
		QT5 = native.getenv("QT5"),
 		GENERATE_PDB = "1",
		CCOPTS = {
			"/W4", "/I.", "/WX", "/DUNICODE", "/D_UNICODE", "/DWIN32", "/D_CRT_SECURE_NO_WARNINGS", "/wd4996", "/wd4389",
			{ "/Od"; Config = "*-*-debug" },
			{ "/O2"; Config = "*-*-release" },
		},
	},
}

Build {

	Passes = {
		GenerateSources = { Name="Generate sources", BuildOrder = 1 },
	},

	Units = "units.lua",

	Configs = {
		Config { Name = "macosx-clang", DefaultOnHost = "macosx", Inherit = macosx, Tools = { "clang-osx" } },
		Config { Name = "win32-msvc", DefaultOnHost = { "windows" }, Inherit = win32, Tools = { "msvc" } },
	},
}
