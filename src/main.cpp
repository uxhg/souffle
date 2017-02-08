/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2013, 2015, Oracle and/or its affiliates. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file main.cpp
 *
 * Main driver for Souffle
 *
 ***********************************************************************/

#include <config.h>
#include <stdlib.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <getopt.h>

#include <chrono>
#include <list>
#include <iostream>
#include <fstream>
#include <string>

#include "AstProgram.h"
#include "AstUtils.h"
#include "AstTuner.h"
#include "AstAnalysis.h"
#include "AstTranslationUnit.h"
#include "AstTransformer.h"
#include "AstSemanticChecker.h"
#include "AstTransforms.h"
#include "BddbddbBackend.h"
#include "PrecedenceGraph.h"
#include "Util.h"
#include "SymbolTable.h"
#include "ParserDriver.h"
#include "ComponentModel.h"
#include "Global.h"

#include "RamTranslator.h"
#include "RamExecutor.h"
#include "RamStatement.h"

namespace souffle {


static void wrapPassesForDebugReporting(std::vector<std::unique_ptr<AstTransformer>> &transforms) {
    for (unsigned int i = 0; i < transforms.size(); i++) {
        transforms[i] = std::unique_ptr<AstTransformer>(new DebugReporter(std::move(transforms[i])));
    }
}

int main(int argc, char **argv)
{
    /* Time taking for overall runtime */
    auto souffle_start = std::chrono::high_resolution_clock::now();

    /* have all to do with command line arguments in its own scope, as these are accessible through the global configuration only */
    {
        Global::config().processArgs(
            argc,
            argv,
            []() {
                std::stringstream header;
                header << "=======================================================================================================" << std::endl;
                header << "souffle -- A datalog engine." << std::endl;
                header << "Usage: souffle [OPTION] FILE." << std::endl;
                header << "-------------------------------------------------------------------------------------------------------" << std::endl;
                header << "Options:" << std::endl;
                return header.str();
            }(),
            []() {
                std::stringstream footer;
                footer << "-------------------------------------------------------------------------------------------------------" << std::endl;
                footer << "Version: " << PACKAGE_VERSION << "" << std::endl;
                footer << "-------------------------------------------------------------------------------------------------------" << std::endl;
                footer << "Copyright (c) 2013, 2015, Oracle and/or its affiliates." << std::endl;
                footer << "All rights reserved." << std::endl;
                footer << "=======================================================================================================" << std::endl;
                return footer.str();
            }(),
            /* new command line options, the environment will be filled with the arguments passed to them, or the empty string if they take none */
            []() {
                MainOption opts[] = {
                    /* each option is { longName, shortName, argument, byDefault, delimiter, description } */
                    // main option, the datalog program, key is always empty
                    {"", 0, "",  "-unknown-", "", ""},
                    // other options
                    {"fact-dir",      'F',  "DIR",  ".",    "", "Specify directory for fact files."},
                    {"include-dir",   'I',  "DIR",  ".", " -I", "Specify directory for include files."},
                    {"output-dir",    'D',  "DIR",  ".",    "", "Specify directory for output relations (if <DIR> is -, output is written to stdout)."},
                    {"jobs",          'j',    "N",  "1",    "", "Run interpreter/compiler in parallel using N threads, N=auto for system default."},
                    {"compile",       'c',     "",   "",    "", "Compile datalog (translating to C++)."},
                    // if the short option is non-alphabetical, it is ommitted from the help text
                    {"auto-schedule", 'a',     "",   "",    "", "Switch on automated clause scheduling for compiler."},
                    {"generate",      'g', "FILE",   "",    "", "Only generate sources of compilable analysis and write it to <FILE>."},
                    {"no-warn",       'w',     "",   "",    "", "Disable warnings."},
                    {"dl-program",    'o', "FILE",   "",    "", "Write executable program to <FILE> (without executing it)."},
                    {"profile",       'p', "FILE",   "",    "", "Enable profiling and write profile data to <FILE>."},
                    {"debug",         'd',     "",   "",    "", "Enable debug mode."},
                    {"bddbddb",       'b', "FILE",   "",    "", "Convert input into bddbddb file format."},
                    // if the short option is non-alphabetical, it is ommitted from the help text
                    {"debug-report",  'r', "FILE",   "",    "", "Write debugging output to HTML report."},
                    {"verbose",       'v',     "",   "",    "", "Verbose output."},
                    {"help",          'h',     "",   "",    "", "Display this help message."}
                    // TODO: this code is depreciated, however is still in use in development -- it will be removed properly once it is no longer needed
                    /*
                    // options for the topological ordering of strongly connected components, see TopologicallySortedSCCGraph class in PrecedenceGraph.cpp
                    {"breadth-limit",   1,    "N",   "",    "", "Specify the breadth limit used for the topological ordering of strongly connected components."},
                    {"depth-limit",     2,    "N",   "",    "", "Specify the depth limit used for the topological ordering of strongly connected components."},
                    {"lookahead",       3,    "N",   "",    "", "Specify the lookahead used for the topological ordering of strongly connected components."},
                    */
                };
                return std::vector<MainOption>(std::begin(opts), std::end(opts));
            }()
        );

        // ------ command line arguments -------------

        /* for the help option, if given simply print the help text then exit */
        if (Global::config().has("help")) {
            ERROR_CALLBACK("unexpected command line argument", []() { std::cerr << Global::config().help(); });
        }

        /* turn on compilation of executables */
        if (Global::config().has("dl-program"))
            Global::config().set("compile");

        /* for the jobs option, to determine the number of threads used */
        if (Global::config().has("jobs")) {
            if (isNumber(Global::config().get("jobs").c_str())) {
                if (std::stoi(Global::config().get("jobs")) < 1)
                       ERROR("Number of jobs in the -j/--jobs options must be greater than zero!");
            } else {
                if (!Global::config().has("jobs", "auto"))
                    ERROR("Wrong parameter " + Global::config().get("jobs") + " for option -j/--jobs!");
                Global::config().set("jobs", "0");
            }
        } else {
            ERROR("Wrong parameter " + Global::config().get("jobs") + " for option -j/--jobs!");
        }

        /* if an output directory is given, check it exists */
        if (Global::config().has("output-dir") && !Global::config().has("output-dir", "-") && !existDir(Global::config().get("output-dir")))
            ERROR("output directory " + Global::config().get("output-dir") + " does not exists");

        /* turn on compilation if auto-scheduling is enabled */
        if (Global::config().has("auto-schedule") && !Global::config().has("compile"))
            Global::config().set("compile");

        /* ensure that if auto-scheduling is enabled an output file is given */
        if (Global::config().has("auto-schedule") && !Global::config().has("dl-program"))
           ERROR("no executable is specified for auto-scheduling (option -o <FILE>)");

        // TODO: this code is depreciated, however is still in use in development -- it will be removed properly once it is no longer needed
        /* set the breadth and depth limits for the topological ordering of strongly connected components */
        /*
        if (Global::config().has("breadth-limit")) {
            int limit = std::stoi(Global::config().get("breadth-limit"));
            if (limit <= 0)
                ERROR("breadth limit must be 1 or more");
            TopologicallySortedSCCGraph::BREADTH_LIMIT = limit;
         }
         if (Global::config().has("depth-limit")) {
            int limit = std::stoi(Global::config().get("depth-limit"));
            if (limit <= 0)
                ERROR("depth limit must be 1 or more");
            TopologicallySortedSCCGraph::DEPTH_LIMIT = limit;
         }
         if (Global::config().has("lookahead")) {
            if (Global::config().has("breadth-limit") || Global::config().has("depth-limit"))
                ERROR("only one of either lookahead or depth-limit and breadth-limit may be specified");
            int lookahead = std::stoi(Global::config().get("lookahead"));
            if (lookahead <= 0)
                ERROR("lookahead must be 1 or more");
            TopologicallySortedSCCGraph::LOOKAHEAD = lookahead;
         }
         */

        /* collect all input directories for the c pre-processor */
        if (Global::config().has("include-dir")) {
            std::string currentInclude = "";
            std::string allIncludes = "";
            for (const char& ch : Global::config().get("include-dir")) {
                if (ch == ' ') {
                    if (!existDir(currentInclude)) {
                        ERROR("include directory " + currentInclude + " does not exists");
                    } else {
                        allIncludes += " -I ";
                        allIncludes += currentInclude;
                    }
                } else {
                    currentInclude += ch;
                }
            }
            Global::config().set("include-dir", allIncludes);
        }

    }

    // ------ start souffle -------------

    std::string programName = which(argv[0]);

    if (programName.empty())
        ERROR("failed to determine souffle executable path");

    /* Create the pipe to establish a communication between cpp and souffle */
    std::string cmd = ::findTool("souffle-mcpp", programName, ".");

    if (!isExecutable(cmd))
        ERROR("failed to locate souffle preprocessor");

    cmd  += " " + Global::config().get("include-dir") + " " + Global::config().get("");
    FILE* in = popen(cmd.c_str(), "r");

    /* Time taking for parsing */
    auto parser_start = std::chrono::high_resolution_clock::now();

    // ------- parse program -------------

    // parse file
    std::unique_ptr<AstTranslationUnit> translationUnit = ParserDriver::parseTranslationUnit("<stdin>", in, Global::config().has("no-warn"));

    // close input pipe
    int preprocessor_status = pclose(in);
    if (preprocessor_status == -1) {
        perror(NULL);
        ERROR("failed to close pre-processor pipe");
    }

    /* Report run-time of the parser if verbose flag is set */
    if (Global::config().has("verbose")) {
        auto parser_end = std::chrono::high_resolution_clock::now();
        std::cout << "Parse Time: " << std::chrono::duration<double>(parser_end-parser_start).count()<< "sec\n";
    }

    // ------- check for parse errors -------------
    if (translationUnit->getErrorReport().getNumErrors() != 0) {
        std::cerr << translationUnit->getErrorReport();
        ERROR(std::to_string(translationUnit->getErrorReport().getNumErrors()) + " errors generated, evaluation aborted");
    }

    // ------- rewriting / optimizations -------------

    std::vector<std::unique_ptr<AstTransformer>> transforms;
    transforms.push_back(std::unique_ptr<AstTransformer>(new ComponentInstantiationTransformer()));
    transforms.push_back(std::unique_ptr<AstTransformer>(new UniqueAggregationVariablesTransformer()));
    transforms.push_back(std::unique_ptr<AstTransformer>(new AstSemanticChecker()));
    if (Global::config().get("bddbddb").empty()) {
    	transforms.push_back(std::unique_ptr<AstTransformer>(new ResolveAliasesTransformer()));
    }
    transforms.push_back(std::unique_ptr<AstTransformer>(new RemoveRelationCopiesTransformer()));
    transforms.push_back(std::unique_ptr<AstTransformer>(new MaterializeAggregationQueriesTransformer()));
    transforms.push_back(std::unique_ptr<AstTransformer>(new RemoveEmptyRelationsTransformer()));
    if (!Global::config().has("debug")) {
        transforms.push_back(std::unique_ptr<AstTransformer>(new RemoveRedundantRelationsTransformer()));
    }
    transforms.push_back(std::unique_ptr<AstTransformer>(new AstExecutionPlanChecker()));
    if (Global::config().has("auto-schedule")) {
        transforms.push_back(std::unique_ptr<AstTransformer>(new AutoScheduleTransformer()));
    }
    if (!Global::config().get("debug-report").empty()) {
        auto parser_end = std::chrono::high_resolution_clock::now();
        std::string runtimeStr = "(" + std::to_string(std::chrono::duration<double>(parser_end-parser_start).count()) + "s)";
        DebugReporter::generateDebugReport(*translationUnit, "Parsing", "After Parsing " + runtimeStr);
        wrapPassesForDebugReporting(transforms);
    }

    for (const auto &transform : transforms) {
        transform->apply(*translationUnit);

        /* Abort evaluation of the program if errors were encountered */
        if (translationUnit->getErrorReport().getNumErrors() != 0) {
            std::cerr << translationUnit->getErrorReport();
            ERROR(std::to_string(translationUnit->getErrorReport().getNumErrors()) + " errors generated, evaluation aborted");
        }
    }
    if (translationUnit->getErrorReport().getNumIssues() != 0) {
        std::cerr << translationUnit->getErrorReport();
    }

    // ------- (optional) conversions -------------

    // conduct the bddbddb file export
    if (!Global::config().get("bddbddb").empty()) {
    	try {
			if (Global::config().get("bddbddb") == "-") {
				// use STD-OUT
				toBddbddb(std::cout,*translationUnit);
			} else {
				// create an output file
				std::ofstream out(Global::config().get("bddbddb").c_str());
				toBddbddb(out,*translationUnit);
			}
    	} catch(const UnsupportedConstructException& uce) {
    	    ERROR("failed to convert input specification into bddbddb syntax because " + std::string(uce.what()));
    	}
    	return 0;
    }


    // ------- execution -------------

    auto ram_start = std::chrono::high_resolution_clock::now();

    /* translate AST to RAM */
    std::unique_ptr<RamStatement> ramProg = RamTranslator(Global::config().has("profile")).translateProgram(*translationUnit);

    if (!Global::config().get("debug-report").empty()) {
        if (ramProg) {
            auto ram_end = std::chrono::high_resolution_clock::now();
            std::string runtimeStr = "(" + std::to_string(std::chrono::duration<double>(ram_end-ram_start).count()) + "s)";
            std::stringstream ramProgStr;
            ramProgStr << *ramProg;
            translationUnit->getDebugReport().addSection(DebugReporter::getCodeSection("ram-program", "RAM Program " + runtimeStr, ramProgStr.str()));
        }

        if (!translationUnit->getDebugReport().empty()) {
            std::ofstream debugReportStream(Global::config().get("debug-report"));
            debugReportStream << translationUnit->getDebugReport();
        }
    }

    /* run RAM program */
    if (!ramProg)
        return 0;

    // pick executor
    std::unique_ptr<RamExecutor> executor;
    if (Global::config().has("generate") || Global::config().has("compile")) {
        /* Locate souffle-compile script */
        std::string compileCmd = ::findTool("souffle-compile", programName, ".");
        /* Fail if a souffle-compile executable is not found */
        if (!isExecutable(compileCmd))
            ERROR("failed to locate souffle-compile");
        compileCmd += " ";
        // configure compiler
        executor = std::unique_ptr<RamExecutor>(new RamCompiler(compileCmd));
        if (Global::config().has("verbose")) {
           executor -> setReportTarget(std::cout);
        }
    } else {
        // configure interpreter
        if (Global::config().has("auto-schedule")) {
            executor = std::unique_ptr<RamExecutor>(new RamGuidedInterpreter());
        } else {
            executor = std::unique_ptr<RamExecutor>(new RamInterpreter());
        }
    }

    // check if this is code generation only
    if (Global::config().has("generate")) {

    	// just generate, no compile, no execute
		static_cast<const RamCompiler*>(executor.get())->generateCode(translationUnit->getSymbolTable(), *ramProg, Global::config().get("generate"));

    	// check if this is a compile only
    } else if (Global::config().has("compile") && Global::config().has("dl-program")) {
        // just compile, no execute
        static_cast<const RamCompiler*>(executor.get())->compileToBinary(translationUnit->getSymbolTable(), *ramProg);
    } else {
        // run executor
        executor->execute(translationUnit->getSymbolTable(), *ramProg);
    }

    /* Report overall run-time in verbose mode */
    if (Global::config().has("verbose")) {
        auto souffle_end = std::chrono::high_resolution_clock::now();
        std::cout << "Total Time: " << std::chrono::duration<double>(souffle_end-souffle_start).count() << "sec\n";
    }

    /// TODO
    BREAKPOINT;
    Global::config().print(std::cerr);

    return 0;
}

} // end of namespace souffle

int main(int argc, char **argv) {
    return souffle::main(argc, argv);
}


