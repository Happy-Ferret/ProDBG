require "tundra.syntax.glob"

Program {
	Name = "Fake6502",

	Sources = { 
		Glob {
			Dir = "examples/Fake6502",
			Extensions = { ".c", ".cpp", ".m" },
		},
	},
}

StaticLibrary {
	Name = "core",

	Env = { CPPPATH = { "src/frontend", "API" } },

	Sources = { 
		Glob {
			Dir = "src/frontend/core",
			Extensions = { ".c", ".cpp", ".m" },
		},
	},
}

---------- Plugins -----------------

SharedLibrary {
	Name = "LLDBPlugin",
	
	Env = {
		CPPPATH = { 
			"API",
			"plugins/lldb",
			"plugins/lldb/Frameworks/LLDB.Framework/Headers",
		},

		SHLIBOPTS = { 
			{ "-Fplugins/lldb/Frameworks", "-lstdc++"; Config = "macosx-clang-*" },
		},

		CXXCOM = { "-std=c++11", "-stdlib=libc++"; Config = "macosx-clang-*" },
	},

	Sources = { 
		Glob {
			Dir = "plugins/lldb",
			Extensions = { ".c", ".cpp", ".m" },
		},
	},

	Frameworks = { "LLDB" },
}

------------------------------------


Program {
	Name = "prodbg-qt5",

	Env = {
		CPPPATH = { 
			".", 
			"API",
			"src/frontend",
			"$(QT5)/include/QtWidgets",
			"$(QT5)/include/QtGui",
			"$(QT5)/include", 
		},
		PROGOPTS = {
			{ "/SUBSYSTEM:WINDOWS", "/DEBUG"; Config = { "win32-*-*", "win64-*-*" } },
		},

		CPPDEFS = {
			{ "PRODBG_MAC", Config = "macosx-*-*" },
			{ "PRODBG_WIN"; Config = { "win32-*-*", "win64-*-*" } },
		},

		CPPOPTS = {
			{ "-Werror", "-pedantic-errors", "-Wall"; Config = "macosx-clang-*" },
		},

		PROGCOM = { 
			-- hacky hacky
			{ "-F$(QT5)/lib", "-lstdc++", "-rpath tundra-output$(SEP)macosx-clang-debug-default"; Config = "macosx-clang-*" },
		},

	},

	Sources = { 
		FGlob {
			Dir = "src/frontend/Qt5",
			Extensions = { ".c", ".cpp", ".m" },
			Filters = {
				{ Pattern = "macosx"; Config = "macosx-*-*" },
				{ Pattern = "windows"; Config = { "win32-*-*", "win64-*-*" } },
			},
		},
	},

	Depends = { "core" },

	Libs = { { "wsock32.lib", "kernel32.lib", "user32.lib", "gdi32.lib", "Comdlg32.lib", "Advapi32.lib" ; Config = "win32-*-*" } },

	Frameworks = { "Cocoa", "QtWidgets", "QtGui", "QtCore"  },
}

Default "LLDBPlugin"
Default "Fake6502"
Default "prodbg-qt5"

