/************************************************************************
 ************************************************************************
    FAUST compiler
    Copyright (C) 2003-2015 GRAME, Centre National de Creation Musicale
    ---------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ************************************************************************
 ************************************************************************/

#include "wast_code_container.hh"
#include "Text.hh"
#include "floats.hh"
#include "exception.hh"
#include "global.hh"
#include "json_instructions.hh"

using namespace std;

/*
 WAST backend and module description:
 
 - mathematical functions are either part of WebAssembly (like f32.sqrt, f32.main, f32.max), are imported from the from JS "global.Math",
   or are externally implemented (log10 in JS using log, fmod in JS)
 - local variables have to be declared first on the block, before being actually initialized or set: this is done using MoveVariablesInFront3
 - 'faustpower' function actually fallback to regular 'pow' (see powprim.h)
 - subcontainers are inlined in 'classInit' and 'instanceConstants' functions
 - waveform generation is 'inlined' using MoveVariablesInFront3, done in a special version of generateInstanceInitFun
 - integer 'min/max' is done in the module in 'min_i/max_i' (using lt/select)
 - memory can be allocated internally in the module and exported, or externally in JS and imported
 - the JSON string is written at offset 0 in a data segment. This string *has* to be converted in a JS string *before* using the DSP instance

*/

dsp_factory_base* WASTCodeContainer::produceFactory()
{
    return new text_dsp_factory_aux(fKlassName, "", "",
                                    gGlobal->gReader.listSrcFiles(),
                                    ((dynamic_cast<std::stringstream*>(fOut)) ? dynamic_cast<std::stringstream*>(fOut)->str() : ""), fHelper.str());
}

WASTCodeContainer::WASTCodeContainer(const string& name, int numInputs, int numOutputs, std::ostream* out, bool internal_memory):fOut(out)
{
    initializeCodeContainer(numInputs, numOutputs);
    fKlassName = name;
    fInternalMemory = internal_memory;
    
    // Allocate one static visitor to be shared by main and sub containers
    if (!gGlobal->gWASTVisitor) {
        gGlobal->gWASTVisitor = new WASTInstVisitor(fOut, fInternalMemory);
    }
}

CodeContainer* WASTCodeContainer::createScalarContainer(const string& name, int sub_container_type)
{
    return new WASTScalarCodeContainer(name, 0, 1, fOut, sub_container_type, true);
}

CodeContainer* WASTCodeContainer::createScalarContainer(const string& name, int sub_container_type, bool internal_memory)
{
    return new WASTScalarCodeContainer(name, 0, 1, fOut, sub_container_type, internal_memory);
}

CodeContainer* WASTCodeContainer::createContainer(const string& name, int numInputs, int numOutputs, ostream* dst, bool internal_memory)
{
    CodeContainer* container;

    if (gGlobal->gMemoryManager) {
        throw faustexception("ERROR : -mem not suported for WebAssembly\n");
    }
    if (gGlobal->gFloatSize == 3) {
        throw faustexception("ERROR : quad format not supported for WebAssembly\n");
    }
    if (gGlobal->gOpenCLSwitch) {
        throw faustexception("ERROR : OpenCL not supported for WebAssembly\n");
    }
    if (gGlobal->gCUDASwitch) {
        throw faustexception("ERROR : CUDA not supported for WebAssembly\n");
    }

    if (gGlobal->gOpenMPSwitch) {
        throw faustexception("ERROR : OpenMP not supported for WebAssembly\n");
    } else if (gGlobal->gSchedulerSwitch) {
        throw faustexception("ERROR : Scheduler mode not supported for WebAssembly\n");
    } else if (gGlobal->gVectorSwitch) {
        throw faustexception("ERROR : Vector mode not supported for WebAssembly\n");
    } else {
        container = new WASTScalarCodeContainer(name, numInputs, numOutputs, dst, kInt, internal_memory);
    }

    return container;
}

// Scalar
WASTScalarCodeContainer::WASTScalarCodeContainer(const string& name, int numInputs, int numOutputs, std::ostream* out, int sub_container_type, bool internal_memory)
    :WASTCodeContainer(name, numInputs, numOutputs, out, internal_memory)
{
     fSubContainerType = sub_container_type;
}

WASTScalarCodeContainer::~WASTScalarCodeContainer()
{}

// Special version that uses MoveVariablesInFront3 to inline waveforms...
DeclareFunInst* WASTCodeContainer::generateInstanceInitFun(const string& name, const string& obj, bool ismethod, bool isvirtual, bool addreturn)
{
    list<NamedTyped*> args;
    if (!ismethod) {
        args.push_back(InstBuilder::genNamedTyped(obj, Typed::kObj_ptr));
    }
    args.push_back(InstBuilder::genNamedTyped("samplingFreq", Typed::kInt32));
    BlockInst* init_block = InstBuilder::genBlockInst();
    
    {
        MoveVariablesInFront3 mover;
        init_block->pushBackInst(mover.getCode(fStaticInitInstructions));
    }
    {
        MoveVariablesInFront3 mover;
        init_block->pushBackInst(mover.getCode(fInitInstructions));
    }
    {
        MoveVariablesInFront3 mover;
        init_block->pushBackInst(mover.getCode(fPostInitInstructions));
    }
    {
        MoveVariablesInFront3 mover;
        init_block->pushBackInst(mover.getCode(fResetUserInterfaceInstructions));
    }
    {
        MoveVariablesInFront3 mover;
        init_block->pushBackInst(mover.getCode(fClearInstructions));
    }
    
    if (addreturn) { init_block->pushBackInst(InstBuilder::genRetInst()); }
    
    // Creates function
    FunTyped* fun_type = InstBuilder::genFunTyped(args, InstBuilder::genBasicTyped(Typed::kVoid), (isvirtual) ? FunTyped::kVirtual : FunTyped::kDefault);
    return InstBuilder::genDeclareFunInst(name, fun_type, init_block);
}

void WASTCodeContainer::produceInternal()
{
    // Fields generation
    generateGlobalDeclarations(gGlobal->gWASTVisitor);
    generateDeclarations(gGlobal->gWASTVisitor);
}

void WASTCodeContainer::produceClass()
{
    int n = 0;
  
    tab(n, *fOut);
    gGlobal->gWASTVisitor->Tab(n);
    
    tab(n, *fOut); *fOut << "(module";
    
        // Global declarations (mathematical functions, global variables...)
        gGlobal->gWASTVisitor->Tab(n+1);
    
        // Sub containers : before functions generation
        mergeSubContainers();
    
        // All mathematical functions (got from math library as variables) have to be first
        generateGlobalDeclarations(gGlobal->gWASTVisitor);
    
        // Exported functions
        tab(n+1, *fOut); *fOut << "(export \"getNumInputs\" (func $getNumInputs))";
        tab(n+1, *fOut); *fOut << "(export \"getNumOutputs\" (func $getNumOutputs))";
        tab(n+1, *fOut); *fOut << "(export \"getSampleRate\" (func $getSampleRate))";
        tab(n+1, *fOut); *fOut << "(export \"init\" (func $init))";
        tab(n+1, *fOut); *fOut << "(export \"instanceInit\" (func $instanceInit))";
        tab(n+1, *fOut); *fOut << "(export \"instanceConstants\" (func $instanceConstants))";
        tab(n+1, *fOut); *fOut << "(export \"instanceResetUserInterface\" (func $instanceResetUserInterface))";
        tab(n+1, *fOut); *fOut << "(export \"instanceClear\" (func $instanceClear))";
        tab(n+1, *fOut); *fOut << "(export \"setParamValue\" (func $setParamValue))";
        tab(n+1, *fOut); *fOut << "(export \"getParamValue\" (func $getParamValue))";
        tab(n+1, *fOut); *fOut << "(export \"compute\" (func $compute))";
    
        // Fields : compute the structure size to use in 'new'
        gGlobal->gWASTVisitor->Tab(n+1);
        generateDeclarations(gGlobal->gWASTVisitor);
        
        // After field declaration...
        generateSubContainers();
    
        // Generate memory
        tab(n+1, *fOut);
        if (fInternalMemory) {
            *fOut << "(memory (export \"memory\") ";
            // Since fJSON is written in date segment at offset 0, the memory size must be computed taking account fJSON size and DSP + audio buffer size
            *fOut << genMemSize(gGlobal->gWASTVisitor->getStructSize(), fNumInputs + fNumOutputs, fJSON.size()) << ")"; // memory initial pages
        } else {
            *fOut << "(import \"memory\" \"memory\" (memory $0 ";
            *fOut << "0))"; // memory size set by JS code, so use a minimum value of 0
        }
    
        // Generate one data segment containing the JSON string starting at offset 0
        tab(n+1, *fOut);
        *fOut << "(data (i32.const 0) \"" << fJSON << "\\00\")";
     
        // Always generated mathematical functions
        tab(n+1, *fOut);
        WASInst::generateIntMin()->accept(gGlobal->gWASTVisitor);
        WASInst::generateIntMax()->accept(gGlobal->gWASTVisitor);
    
        // getNumInputs/getNumOutputs
        generateGetInputs("getNumInputs", "dsp", false, false)->accept(gGlobal->gWASTVisitor);
        generateGetOutputs("getNumOutputs", "dsp", false, false)->accept(gGlobal->gWASTVisitor);
    
        // Inits
        tab(n+1, *fOut); *fOut << "(func $classInit (param $dsp i32) (param $samplingFreq i32)";
            tab(n+2, *fOut); gGlobal->gWASTVisitor->Tab(n+2);
            {
                // Rename 'sig' in 'dsp', remove 'dsp' allocation, inline subcontainers 'instanceInit' and 'fill' function call
                DspRenamer renamer;
                BlockInst* renamed = renamer.getCode(fStaticInitInstructions);
                BlockInst* inlined = inlineSubcontainersFunCalls(renamed);
                generateWASTBlock(inlined);
            }
        tab(n+1, *fOut); *fOut << ")";
    
        tab(n+1, *fOut); *fOut << "(func $instanceConstants (param $dsp i32) (param $samplingFreq i32)";
            tab(n+2, *fOut); gGlobal->gWASTVisitor->Tab(n+2);
            {
                // Rename 'sig' in 'dsp', remove 'dsp' allocation, inline subcontainers 'instanceInit' and 'fill' function call
                DspRenamer renamer;
                BlockInst* renamed = renamer.getCode(fInitInstructions);
                BlockInst* inlined = inlineSubcontainersFunCalls(renamed);
                generateWASTBlock(inlined);
            }
        tab(n+1, *fOut); *fOut << ")";
    
        tab(n+1, *fOut); *fOut << "(func $instanceResetUserInterface (param $dsp i32)";
            tab(n+2, *fOut); gGlobal->gWASTVisitor->Tab(n+2);
            {
                // Rename 'sig' in 'dsp' and remove 'dsp' allocation
                DspRenamer renamer;
                generateWASTBlock(renamer.getCode(fResetUserInterfaceInstructions));
            }
        tab(n+1, *fOut); *fOut << ")";
    
        tab(n+1, *fOut); *fOut << "(func $instanceClear (param $dsp i32)";
            tab(n+2, *fOut); gGlobal->gWASTVisitor->Tab(n+2);
            {
                // Rename 'sig' in 'dsp' and remove 'dsp' allocation
                DspRenamer renamer;
                generateWASTBlock(renamer.getCode(fClearInstructions));
            }
        tab(n+1, *fOut); *fOut << ")";
    
        gGlobal->gWASTVisitor->Tab(n+1);
    
        // init
        generateInit("dsp", false, false)->accept(gGlobal->gWASTVisitor);
    
        // instanceInit
        generateInstanceInit("dsp", false, false)->accept(gGlobal->gWASTVisitor);
    
        // getSampleRate
        generateGetSampleRate("dsp", false, false)->accept(gGlobal->gWASTVisitor);
    
        // setParamValue
        tab(n+1, *fOut); *fOut << "(func $setParamValue (param $dsp i32) (param $index i32) (param $value " << realStr << ")";
            tab(n+2, *fOut); *fOut << "(" << realStr << ".store ";
                tab(n+3, *fOut); *fOut << "(i32.add (get_local $dsp) (get_local $index))";
                tab(n+3, *fOut); *fOut << "(get_local $value)";
            tab(n+2, *fOut); *fOut << ")";
        tab(n+1, *fOut); *fOut << ")";
    
        // getParamValue
        tab(n+1, *fOut); *fOut << "(func $getParamValue (param $dsp i32) (param $index i32) (result " << realStr << ")";
            tab(n+2, *fOut); *fOut << "(return (" << realStr << ".load (i32.add (get_local $dsp) (get_local $index))))";
        tab(n+1, *fOut); *fOut << ")";
    
        // compute
        generateCompute(n);
        
        // Possibly generate separated functions
        gGlobal->gWASTVisitor->Tab(n+1);
        tab(n+1, *fOut);
        generateComputeFunctions(gGlobal->gWASTVisitor);
    
    tab(n, *fOut); *fOut << ")";
    tab(n, *fOut);
    
    // Helper code
    
    // Generate JSON and getSize
    map <string, string>::iterator it;
    tab(n, fHelper); fHelper << "/*\n" << "Code generated with Faust version " << FAUSTVERSION << endl;
    fHelper << "Compilation options: ";
    printCompilationOptions(fHelper);
    fHelper << "\n*/\n";
    tab(n, fHelper); fHelper << "function getSize" << fKlassName << "() {";
        tab(n+1, fHelper);
        fHelper << "return " << gGlobal->gWASTVisitor->getStructSize() << ";";
        printlines(n+1, fUICode, fHelper);
    tab(n, fHelper); fHelper << "}";
    tab(n, fHelper);
    
    // Fields to path
    tab(n, fHelper); fHelper << "function getPathTable" << fKlassName << "() {";
        tab(n+1, fHelper); fHelper << "var pathTable = [];";
        map <string, MemoryDesc>& fieldTable = gGlobal->gWASTVisitor->getFieldTable();
        for (it = fJSONVisitor.fPathTable.begin(); it != fJSONVisitor.fPathTable.end(); it++) {
            MemoryDesc tmp = fieldTable[(*it).first];
            tab(n+1, fHelper); fHelper << "pathTable[\"" << (*it).second << "\"] = " << tmp.fOffset << ";";
        }
        tab(n+1, fHelper); fHelper << "return pathTable;";
    tab(n, fHelper); fHelper << "}";
    
    // Generate JSON
    tab(n, fHelper);
    tab(n, fHelper); fHelper << "function getJSON" << fKlassName << "() {";
        tab(n+1, fHelper);
        fHelper << "return \""; fHelper << fJSON; fHelper << "\";";
        printlines(n+1, fUICode, fHelper);
    tab(n, fHelper); fHelper << "}";
    
    // Metadata declaration
    tab(n, fHelper);
    tab(n, fHelper); fHelper << "function metadata" << fKlassName << "(m) {";
    for (map<Tree, set<Tree> >::iterator i = gGlobal->gMetaDataSet.begin(); i != gGlobal->gMetaDataSet.end(); i++) {
        if (i->first != tree("author")) {
            tab(n+1, fHelper); fHelper << "m.declare(\"" << *(i->first) << "\", " << **(i->second.begin()) << ");";
        } else {
            for (set<Tree>::iterator j = i->second.begin(); j != i->second.end(); j++) {
                if (j == i->second.begin()) {
                    tab(n+1, fHelper); fHelper << "m.declare(\"" << *(i->first) << "\", " << **j << ");" ;
                } else {
                    tab(n+1, fHelper); fHelper << "m.declare(\"" << "contributor" << "\", " << **j << ");";
                }
            }
        }
    }
    tab(n, fHelper); fHelper << "}" << endl << endl;
}

void WASTScalarCodeContainer::generateCompute(int n)
{
    tab(n+1, *fOut); *fOut << "(func $compute (param $dsp i32) (param $count i32) (param $inputs i32) (param $outputs i32)";
        tab(n+2, *fOut);
        gGlobal->gWASTVisitor->Tab(n+2);
        fComputeBlockInstructions->pushBackInst(fCurLoop->generateScalarLoop(fFullCount));
        MoveVariablesInFront2 mover;
        BlockInst* block = mover.getCode(fComputeBlockInstructions, true);
        block->accept(gGlobal->gWASTVisitor);
    tab(n+1, *fOut); *fOut << ")";
}

DeclareFunInst* WASInst::generateIntMin()
{
    string v1 = gGlobal->getFreshID("v1");
    string v2 = gGlobal->getFreshID("v2");
    
    list<NamedTyped*> args;
    args.push_back(InstBuilder::genNamedTyped(v1, Typed::kInt32));
    args.push_back(InstBuilder::genNamedTyped(v2, Typed::kInt32));
    
    BlockInst* block = InstBuilder::genBlockInst();
    block->pushBackInst(InstBuilder::genRetInst(InstBuilder::genSelect2Inst(InstBuilder::genLessThan(InstBuilder::genLoadFunArgsVar(v1),
                                                                                                     InstBuilder::genLoadFunArgsVar(v2)),
                                                                            InstBuilder::genLoadFunArgsVar(v1),
                                                                            InstBuilder::genLoadFunArgsVar(v2))));
    // Creates function
    FunTyped* fun_type = InstBuilder::genFunTyped(args, InstBuilder::genBasicTyped(Typed::kInt32), FunTyped::kDefault);
    return InstBuilder::genDeclareFunInst("min_i", fun_type, block);
}

DeclareFunInst* WASInst::generateIntMax()
{
    string v1 = gGlobal->getFreshID("v1");
    string v2 = gGlobal->getFreshID("v2");
    
    list<NamedTyped*> args;
    args.push_back(InstBuilder::genNamedTyped(v1, Typed::kInt32));
    args.push_back(InstBuilder::genNamedTyped(v2, Typed::kInt32));
    
    BlockInst* block = InstBuilder::genBlockInst();
    block->pushBackInst(InstBuilder::genRetInst(InstBuilder::genSelect2Inst(InstBuilder::genLessThan(InstBuilder::genLoadFunArgsVar(v1),
                                                                                                     InstBuilder::genLoadFunArgsVar(v2)),
                                                                            InstBuilder::genLoadFunArgsVar(v2),
                                                                            InstBuilder::genLoadFunArgsVar(v1))));
    // Creates function
    FunTyped* fun_type = InstBuilder::genFunTyped(args, InstBuilder::genBasicTyped(Typed::kInt32), FunTyped::kDefault);
    return InstBuilder::genDeclareFunInst("max_i", fun_type, block);
}

// Vector
WASTVectorCodeContainer::WASTVectorCodeContainer(const string& name, int numInputs, int numOutputs, std::ostream* out, bool internal_memory)
    :WASTCodeContainer(name, numInputs, numOutputs, out, internal_memory)
{
    // No array on stack, move all of them in struct
    gGlobal->gMachineMaxStackSize = -1;
}

WASTVectorCodeContainer::~WASTVectorCodeContainer()
{}

void WASTVectorCodeContainer::generateCompute(int n)
{}