/************************************************************************
 ************************************************************************
    FAUST compiler
    Copyright (C) 2003-2018 GRAME, Centre National de Creation Musicale
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

#ifndef _FBC_CPP_COMPILER_H
#define _FBC_CPP_COMPILER_H

#include <map>
#include <string>
#include <iostream>
#include <sstream>
#include <vector>

#include "fbc_interpreter.hh"
#include "interpreter_bytecode.hh"

using namespace std;

static void tab(int n, ostream& fout)
{
    fout << '\n';
    while (n--) fout << '\t';
}

#define dispatchReturn()  \
    {                     \
        it = popAddr();   \
    }
#define saveReturn()      \
    {                     \
        pushAddr(it + 1); \
    }

// C++ instructions block
struct CPPBlock : public std::vector<std::string> {
    
    std::string fNum;
    
    CPPBlock(int block_index):fNum(std::to_string(block_index))
    {}
    
    void print(int n, std::ostream& out)
    {
        // header
        tab(n, out); out << "{";
        tab(n+1, out); out << "label" << fNum << ":";
        
        // block
        if (this->size() == 0) {
            tab(n+1, out); out << ";";
        } else {
            for (auto& it : *this) {
                tab(n+1, out); out << it;
            }
        }
        
        // footer
        tab(n, out); out << "}";
    }
 
};

// C++ blocks list
struct CPPBlockList : public std::vector<CPPBlock> {

    int fCurrent;
    
    CPPBlockList():fCurrent(0)
    {}
    
    void print(int n, std::ostream& out)
    {
        for (auto& it : *this) {
            it.print(n, out);
        }
    }
    
    void addBlock()
    {
        push_back(CPPBlock(fCurrent++));
    }
    
    void addInst(const string& code)
    {
        at(fCurrent-1).push_back(code);
    }
    
    void addPreviousInst(const string& code)
    {
        at(fCurrent-2).push_back(code);
    }
  
    std::string getIndex()
    {
        return at(fCurrent-1).fNum;
    }
    
};

// FBC C++ compiler
template <class T>
class FBCCPPCompiler {
    protected:
    
    string        fCPPStack[512];
    InstructionIT fAddressStack[64];
 
    int fCPPStackIndex;
    int fAddrStackIndex;
    
    CPPBlockList fBlockList;
    
    void pushBinop(const std::string& op)
    {
        std::string v1 = popValue();
        std::string v2 = popValue();
        pushValue("(" + v1 + " " + op + " " + v2 + ")");
    }

    std::string genFloat(float num)
    {
        std::stringstream str;
        str << std::setprecision(std::numeric_limits<T>::max_digits10) << num;
        return str.str();
    }
    std::string genDouble(double num)
    {
        std::stringstream str;
        str << std::setprecision(std::numeric_limits<T>::max_digits10) << num;
        return str.str();
    }
    std::string genReal(double num)     { return (sizeof(T) == sizeof(double)) ? genDouble(num) : genFloat(num); }
    std::string genInt32(int num)       { return std::to_string(num); }
    std::string genInt64(long long num) { return std::to_string(num); }

    void        pushValue(const std::string& val) { fCPPStack[fCPPStackIndex++] = val; }
    std::string popValue() { return fCPPStack[--fCPPStackIndex]; }

    void          pushAddr(InstructionIT addr) { fAddressStack[fAddrStackIndex++] = addr; }
    InstructionIT popAddr() { return fAddressStack[--fAddrStackIndex]; }
    bool          emptyReturn() { return (fAddrStackIndex == 0); }

    void pushUnaryCall(const std::string& name)
    {
        pushValue(name + "(" + popValue() + ")");
    }
    void pushBinaryCall(const std::string& name)
    {
        pushValue(name + "(" + popValue() + ", " + popValue() + ")");
    }

    void pushLoadArray(const std::string& array, int index)
    {
        pushValue(array + "[" + std::to_string(index) + "]");
    }
    void pushStoreArray(const std::string& array, int index)
    {
        fBlockList.addInst(array + "[" + std::to_string(index) + "] = " + popValue() + ";");
    }
    void pushLoadArray(const std::string& array, const std::string& index)
    {
        pushValue(array + "[" + index + "]");
    }
    void pushStoreArray(const std::string& array,const std::string& index)
    {
        fBlockList.addInst(array + "[" + index + "] = " + popValue() + ";");
    }

    void pushLoadInput(int index)
    {
        pushValue(getRealTy() + "(inputs[" + std::to_string(index) + "][" + popValue() + "])");
    }
    void pushStoreOutput(int index)
    {
        fBlockList.addInst("outputs[" + std::to_string(index) + "][" + popValue() + "] = FAUSTFLOAT(" + popValue() + ");");
    }

    void CompileBlock(FBCBlockInstruction<T>* block)
    {
        InstructionIT it  = block->fInstructions.begin();
        bool          end = false;
   
        while ((it != block->fInstructions.end()) && !end) {
            //(*it)->write(&std::cout);

            switch ((*it)->fOpcode) {
                    // Numbers
                case FBCInstruction::kRealValue:
                    pushValue(genReal((*it)->fRealValue));
                    it++;
                    break;

                case FBCInstruction::kInt32Value:
                    pushValue(genInt32((*it)->fIntValue));
                    it++;
                    break;

                    // Memory load/store
                case FBCInstruction::kLoadReal:
                    pushLoadArray("fRealHeap", (*it)->fOffset1);
                    it++;
                    break;

                case FBCInstruction::kLoadInt:
                    pushLoadArray("fIntHeap", (*it)->fOffset1);
                    it++;
                    break;

                case FBCInstruction::kStoreReal:
                    pushStoreArray("fRealHeap", (*it)->fOffset1);
                    it++;
                    break;

                case FBCInstruction::kStoreInt:
                    pushStoreArray("fIntHeap", (*it)->fOffset1);
                    it++;
                    break;

                    // Indexed memory load/store: constant values are added at generation time by CreateBinOp...
                case FBCInstruction::kLoadIndexedReal: {
                    std::string offset = genInt32((*it)->fOffset1) + "+" + popValue();
                    pushLoadArray("fRealHeap", offset);
                    it++;
                    break;
                }

                case FBCInstruction::kLoadIndexedInt: {
                    std::string offset = genInt32((*it)->fOffset1) + "+" + popValue();
                    pushLoadArray("fIntHeap", offset);
                    it++;
                    break;
                }

                case FBCInstruction::kStoreIndexedReal: {
                    std::string offset = genInt32((*it)->fOffset1) + "+" + popValue();
                    pushStoreArray("fRealHeap", offset);
                    it++;
                    break;
                }

                case FBCInstruction::kStoreIndexedInt: {
                    std::string offset = genInt32((*it)->fOffset1) + "+" + popValue();
                    pushStoreArray("fIntHeap", offset);
                    it++;
                    break;
                }

                    // Memory shift (TODO : use memmove ?)
                case FBCInstruction::kBlockShiftReal: {
                    for (int i = (*it)->fOffset1; i > (*it)->fOffset2; i -= 1) {
                        pushLoadArray("fRealHeap", i - 1);
                        pushStoreArray("fRealHeap", i);
                    }
                    it++;
                    break;
                }

                case FBCInstruction::kBlockShiftInt: {
                    for (int i = (*it)->fOffset1; i > (*it)->fOffset2; i -= 1) {
                        pushLoadArray("fIntHeap", i - 1);
                        pushStoreArray("fIntHeap", i);
                    }
                    it++;
                    break;
                }

                    // Input/output
                case FBCInstruction::kLoadInput:
                    pushLoadInput((*it)->fOffset1);
                    it++;
                    break;

                case FBCInstruction::kStoreOutput:
                    pushStoreOutput((*it)->fOffset1);
                    it++;
                    break;

                    // Cast
                case FBCInstruction::kCastReal: {
                    std::string val = popValue();
                    pushValue(getRealTy() + "(" + val + ")");
                    it++;
                    break;
                }

                case FBCInstruction::kCastInt: {
                    std::string val = popValue();
                    pushValue("int(" + val + ")");
                    it++;
                    break;
                }

                case FBCInstruction::kBitcastInt: {
                    std::string val = popValue();
                    pushValue("*reinterpret_cast<int*>(" + val + ")");
                    it++;
                    break;
                }

                case FBCInstruction::kBitcastReal: {
                    std::string val = popValue();
                    pushValue("*reinterpret_cast<" + getRealTy() + "*>(" + val + ")");
                    it++;
                    break;
                }

                    // Binary math
                case FBCInstruction::kAddReal:
                case FBCInstruction::kAddInt:
                    pushBinop("+");
                    it++;
                    break;

                case FBCInstruction::kSubReal:
                    case FBCInstruction::kSubInt:
                    pushBinop("-");
                    it++;
                    break;

                case FBCInstruction::kMultReal:
                case FBCInstruction::kMultInt:
                    pushBinop("*");
                    it++;
                    break;

                case FBCInstruction::kDivReal:
                    case FBCInstruction::kDivInt:
                    pushBinop("/");
                    it++;
                    break;

                case FBCInstruction::kRemReal:
                    pushBinaryCall("std::remainder");
                    it++;
                    break;

                case FBCInstruction::kRemInt:
                    pushBinop("%");
                    it++;
                    break;

                case FBCInstruction::kLshInt:
                    pushBinop("<<");
                    it++;
                    break;

                case FBCInstruction::kRshInt:
                    pushBinop(">>");
                    it++;
                    break;

                case FBCInstruction::kGTInt:
                case FBCInstruction::kGTReal:
                    pushBinop(">");
                    it++;
                    break;

                case FBCInstruction::kLTInt:
                case FBCInstruction::kLTReal:
                    pushBinop("<");
                    it++;
                    break;

                case FBCInstruction::kGEInt:
                case FBCInstruction::kGEReal:
                    pushBinop(">=");
                    it++;
                    break;

                case FBCInstruction::kLEInt:
                case FBCInstruction::kLEReal:
                    pushBinop("<=");
                    it++;
                    break;

                case FBCInstruction::kEQInt:
                case FBCInstruction::kEQReal:
                    pushBinop("==");
                    it++;
                    break;

                case FBCInstruction::kNEInt:
                case FBCInstruction::kNEReal:
                    pushBinop("!=");
                    it++;
                    break;

                case FBCInstruction::kANDInt:
                    pushBinop("&");
                    it++;
                    break;

                case FBCInstruction::kORInt:
                    pushBinop("|");
                    it++;
                    break;

                case FBCInstruction::kXORInt:
                    pushBinop("^");
                    it++;
                    break;

                    // Extended unary math
                case FBCInstruction::kAbs:
                    pushUnaryCall("std::abs");
                    it++;
                    break;

                case FBCInstruction::kAbsf:
                    pushUnaryCall("std::fabs");
                    it++;
                    break;

                case FBCInstruction::kAcosf:
                    pushUnaryCall("std::acos");
                    it++;
                    break;

                case FBCInstruction::kAsinf:
                    pushUnaryCall("std::asin");
                    it++;
                    break;

                case FBCInstruction::kAtanf:
                    pushUnaryCall("std::atan");
                    it++;
                    break;

                case FBCInstruction::kCeilf:
                    pushUnaryCall("std::ceil");
                    it++;
                    break;

                case FBCInstruction::kCosf:
                    pushUnaryCall("std::cos");
                    it++;
                    break;

                case FBCInstruction::kCoshf:
                    pushUnaryCall("std::cosh");
                    it++;
                    break;

                case FBCInstruction::kExpf:
                    pushUnaryCall("std::exp");
                    it++;
                    break;

                case FBCInstruction::kFloorf:
                    pushUnaryCall("std::floor");
                    it++;
                    break;

                case FBCInstruction::kLogf:
                    pushUnaryCall("std::log");
                    it++;
                    break;

                case FBCInstruction::kLog10f:
                    pushUnaryCall("std::log10");
                    it++;
                    break;

                case FBCInstruction::kRoundf:
                    pushUnaryCall("std::round");
                    it++;
                    break;

                case FBCInstruction::kSinf:
                    pushUnaryCall("std::sin");
                    it++;
                    break;

                case FBCInstruction::kSinhf:
                    pushUnaryCall("std::sinh");
                    it++;
                    break;

                case FBCInstruction::kSqrtf:
                    pushUnaryCall("sstd::qrt");
                    it++;
                    break;

                case FBCInstruction::kTanf:
                    pushUnaryCall("std::tan");
                    it++;
                    break;

                case FBCInstruction::kTanhf:
                    pushUnaryCall("std::tanh");
                    it++;
                    break;

                    // Extended binary math
                case FBCInstruction::kAtan2f:
                    pushBinaryCall("std::atan2");
                    it++;
                    break;

                case FBCInstruction::kFmodf:
                    pushBinaryCall("std::fmod");
                    it++;
                    break;

                case FBCInstruction::kPowf:
                    pushBinaryCall("std::pow");
                    it++;
                    break;

                case FBCInstruction::kMax:
                    pushBinaryCall("std::max<int>");
                    it++;
                    break;
                    
                case FBCInstruction::kMaxf:
                    pushBinaryCall("std::max<" + getRealTy() + ">");
                    it++;
                    break;

                case FBCInstruction::kMin:
                    pushBinaryCall("std::min<int>");
                    it++;
                    break;

                case FBCInstruction::kMinf:
                    pushBinaryCall("std::min<" + getRealTy() + ">");
                    it++;
                    break;
 
                    // Control
                case FBCInstruction::kReturn:
                    // Empty addr stack = end of computation
                    if (emptyReturn()) {
                        //fCurrentBlock->endBlock();
                        end = true;
                    } else {
                        dispatchReturn();
                    }
                    break;

                // TO CHECK
                case FBCInstruction::kIf: {
                    /*
                    saveReturn();
                    LLVMValue cond_value = fBuilder->CreateICmpEQ(popValue(), genInt32(1), "ifcond");
                    Function* function   = fBuilder->GetInsertBlock()->getParent();

                    // Create blocks for the then and else cases.  Insert the 'then' block at the end of the function
                    BasicBlock* then_block  = BasicBlock::Create(fModule->getContext(), "then_code", function);
                    BasicBlock* else_block  = BasicBlock::Create(fModule->getContext(), "else_code");
                    BasicBlock* merge_block = BasicBlock::Create(fModule->getContext(), "merge_block");

                    pushValue(fBuilder->CreateCondBr(cond_value, then_block, else_block));

                    // Compile then branch (= branch1)
                    CompileBlock((*it)->fBranch1, then_block);

                    fBuilder->CreateBr(merge_block);
                    // Codegen of 'Then' can change the current block, update then_block for the PHI
                    then_block = fBuilder->GetInsertBlock();

                    // Emit else block
                    function->getBasicBlockList().push_back(else_block);

                    // Compile else branch (= branch2)
                    CompileBlock((*it)->fBranch2, else_block);

                    pushValue(fBuilder->CreateBr(merge_block));
                    // Codegen of 'Else' can change the current block, update else_block for the PHI
                    else_block = fBuilder->GetInsertBlock();

                    // Emit merge block
                    function->getBasicBlockList().push_back(merge_block);
                    fBuilder->SetInsertPoint(merge_block);
                    */
                    
                    break;
                }

                case FBCInstruction::kSelectReal:
                case FBCInstruction::kSelectInt: {
                    
                    // Prepare condition
                    std::string cond_value = popValue();

                    // Compile then branch (= branch1)
                    CompileBlock((*it)->fBranch1);

                    // Compile else branch (= branch2)
                    CompileBlock((*it)->fBranch2);

                    // Create the result (= branch2)
                    std::string then_value = popValue();
                    std::string else_value = popValue();
                    pushValue("(" + cond_value + ") ? " + else_value + " : " + then_value);

                    it++;
                    break;
                }

                case FBCInstruction::kCondBranch: {
                    
                    // Prepare condition
                    std::string cond = popValue();
                    
                    // Get current block index
                    std::string id1 = fBlockList.getIndex();

                    // New block for loop
                    fBlockList.addBlock();
                    
                    // Get current block index
                    std::string id2 = fBlockList.getIndex();

                    // Branch to current block
                    fBlockList.addPreviousInst("if " + cond + " { goto label" + id1 + "; } else { goto label" + id2 + "; }");
               
                    it++;
                    break;
                }

                case FBCInstruction::kLoop: {

                    // New block for condition
                    fBlockList.addBlock();
                    
                    // Compile init branch (= branch1)
                    CompileBlock((*it)->fBranch1);

                    // New block for loop
                    fBlockList.addBlock();
                    
                    // Compile loop branch (= branch2)
                    CompileBlock((*it)->fBranch2);

                    it++;
                    break;
                }

                default:
                    // Should not happen
                    //(*it)->write(&std::cout);
                    it++;
                    //faustassert(false);
                    break;
            }
        }
    }

   public:
    FBCCPPCompiler():fCPPStackIndex(0), fAddrStackIndex(0)
    {}
    
    virtual ~FBCCPPCompiler()
    {}
    
    void CompileBlock(FIRUserInterfaceBlockInstruction<T>* block, int n, ostream& out)
    {
        UIInstructionIT it;
        
        for (it = block->fInstructions.begin(); it != block->fInstructions.end(); it++) {
            //(*it)->write(&std::cout);
            
            switch ((*it)->fOpcode) {
                case FBCInstruction::kOpenVerticalBox:
                    tab(n, out); out << "ui_interface->openVerticalBox(\"" << (*it)->fLabel << "\");";
                    break;
                    
                case FBCInstruction::kOpenHorizontalBox:
                    tab(n, out); out << "ui_interface->openHorizontalBox(\"" << (*it)->fLabel << "\");";
                    break;
                    
                case FBCInstruction::kOpenTabBox:
                    tab(n, out); out << "ui_interface->openTabBox(\"" << (*it)->fLabel << "\");";
                    break;
                    
                case FBCInstruction::kCloseBox:
                    tab(n, out); out << "ui_interface->closeBox();";
                    break;
                    
                case FBCInstruction::kAddButton:
                    tab(n, out); out << "ui_interface->addButton(\"" << (*it)->fLabel << "\", &fRealHeap[" << (*it)->fOffset << "]);";
                    break;
                    
                case FBCInstruction::kAddCheckButton:
                    tab(n, out); out << "ui_interface->addCheckButton(\"" << (*it)->fLabel << "\", &fRealHeap[" << (*it)->fOffset << "]);";
                    break;
                    
                case FBCInstruction::kAddHorizontalSlider:
                    tab(n, out);
                    out << "ui_interface->addHorizontalSlider(\"";
                    out << (*it)->fLabel << "\", &fRealHeap[" << (*it)->fOffset << "], ";
                    out << (*it)->fInit << ", ";
                    out << (*it)->fMin << ", ";
                    out << (*it)->fMax << ", ";
                    out << (*it)->fStep << ");";
                    break;
                    
                case FBCInstruction::kAddVerticalSlider:
                    tab(n, out);
                    out << "ui_interface->addVerticalSlider(\"";
                    out << (*it)->fLabel << "\", &fRealHeap[" << (*it)->fOffset << "], ";
                    out << (*it)->fInit << ", ";
                    out << (*it)->fMin << ", ";
                    out << (*it)->fMax << ", ";
                    out << (*it)->fStep << ");";
                    break;
                    
                case FBCInstruction::kAddNumEntry:
                    tab(n, out);
                    out << "ui_interface->addNumEntry(\"";
                    out << (*it)->fLabel << "\", &fRealHeap[" << (*it)->fOffset << "], ";
                    out << (*it)->fInit << ", ";
                    out << (*it)->fMin << ", ";
                    out << (*it)->fMax << ", ";
                    out << (*it)->fStep << ");";
                    break;
                    
                case FBCInstruction::kAddSoundFile:
                    tab(n, out); out << "// TODO";
                    break;
                    
                case FBCInstruction::kAddHorizontalBargraph:
                    tab(n, out);
                    out << "ui_interface->addHorizontalBargraph(\"";
                    out << (*it)->fLabel << "\", &fRealHeap[" << (*it)->fOffset << "], ";
                    out << (*it)->fMin << ", ";
                    out << (*it)->fMax << ");";
                    break;
                    
                case FBCInstruction::kAddVerticalBargraph:
                    tab(n, out);
                    out << "ui_interface->addVerticalBargraph(\"";
                    out << (*it)->fLabel << "\", &fRealHeap[" << (*it)->fOffset << "], ";
                    out << (*it)->fMin << ", ";
                    out << (*it)->fMax << ");";
                    break;
                    
                case FBCInstruction::kDeclare:
                    // Special case for "0" zone
                    tab(n, out);
                    if ((*it)->fOffset == -1) {
                        out << "ui_interface->declare(0, \"" << (*it)->fKey << "\", \"" << (*it)->fValue << "\");";
                    } else {
                        out << "ui_interface->declare(&fRealHeap[" << (*it)->fOffset << "], \"" << (*it)->fKey << "\", \"" << (*it)->fValue << "\");";
                    }
                    break;
                    
                default:
                    break;
            }
        }
    
    }
    
    void CompileBlock(FIRMetaBlockInstruction* block, int n, ostream& out)
    {
        MetaInstructionIT it;
        
        for (it = block->fInstructions.begin(); it != block->fInstructions.end(); it++) {
            //(*it)->write(&std::cout);
            
            tab(n, out);
            out << "m->declare(\"" << (*it)->fKey << "\", \"" << (*it)->fValue << "\");";
        }
    }
    
    void CompileBlock(FBCBlockInstruction<T>* block, int n, ostream& out, bool print = true)
    {
        // Compile function body
        fBlockList.addBlock();
        CompileBlock(block);
        
        // Generate block list
        if (print) { fBlockList.print(n, out); }
    }
    
    static std::string getRealTy() { return (sizeof(T) == sizeof(double)) ? "double" : "float"; }
    
};

// FBC C++ code generator
template <class T>
class FBCCPPGenerator : public FBCInterpreter<T, 0> {
  
  public:
    
    FBCCPPGenerator(interpreter_dsp_factory_aux<T, 0>* factory)
    : FBCInterpreter<T, 0>(factory)
    {}
    
    virtual ~FBCCPPGenerator()
    {}
    
    void generateCode(std::ostream& out)
    {
        int tabs = 0;
        tab(tabs, out);
        out << "#include <cmath>";
        tab(tabs, out);
        tab(tabs, out);
        out << "class mydsp : public dsp {";
            tab(tabs, out);
            tab(tabs, out);
            out << "  private:";
        
            tab(tabs+1, out);
            tab(tabs+1, out);
            out << "int fIntHeap[" << this->fFactory->fIntHeapSize << "];";
            tab(tabs+1, out);
            out << FBCCPPCompiler<T>::getRealTy() << " fRealHeap[" << this->fFactory->fRealHeapSize << "];";
        
            tab(tabs, out);
            tab(tabs, out);
            out << "  public:";
            tab(tabs+1, out);
        
            tab(tabs+1, out);
            out << "virtual int getNumInputs() { return " << this->fFactory->fNumInputs << "; }";
            tab(tabs+1, out);
            out << "virtual int getNumOutputs() { return " << this->fFactory->fNumOutputs << "; }";
        
            tab(tabs+1, out);
            tab(tabs+1, out);
            out << "virtual void buildUserInterface(UI* ui_interface)";
            tab(tabs+1, out);
            out << "{";
            {
                FBCCPPCompiler<T> compiler;
                compiler.CompileBlock(this->fFactory->fUserInterfaceBlock, tabs+2, out);
            }
            tab(tabs+1, out);
            out << "}";
        
            tab(tabs+1, out);
            tab(tabs+1, out);
            out << "virtual void classInit(int samplingRate)";
            tab(tabs+1, out);
            out << "{";
            {
                FBCCPPCompiler<T> compiler;
                compiler.CompileBlock(this->fFactory->fStaticInitBlock, tabs+2, out);
            }
            tab(tabs+1, out);
            out << "}";
        
            tab(tabs+1, out);
            tab(tabs+1, out);
            out << "virtual int getSampleRate() { return fIntHeap[" << this->fFactory->fSROffset << "]; }";
        
            tab(tabs+1, out);
            tab(tabs+1, out);
            out << "virtual void init(int samplingFreq)";
            tab(tabs+1, out);
            out << "{";
                tab(tabs+2, out);
                out << "classInit(samplingFreq);";
                tab(tabs+2, out);
                out << "instanceInit(samplingFreq);";
                tab(tabs+2, out);
            out << "}";
        
            tab(tabs+1, out);
            tab(tabs+1, out);
            out << "virtual void instanceInit(int samplingFreq)";
            tab(tabs+1, out);
            out << "{";
                tab(tabs+2, out);
                out << "instanceConstants(samplingFreq);";
                tab(tabs+2, out);
                out << "instanceResetUserInterface();";
                tab(tabs+2, out);
                out << "instanceClear();";
                tab(tabs+2, out);
            out << "}";
        
            tab(tabs+1, out);
            tab(tabs+1, out);
            out << "virtual void instanceConstants(int samplingRate)";
            tab(tabs+1, out);
            out << "{";
                tab(tabs+2, out);
                out << "fIntHeap[" << this->fFactory->fSROffset << "] = samplingRate;";
            {
                FBCCPPCompiler<T> compiler;
                compiler.CompileBlock(this->fFactory->fInitBlock, tabs+2, out);
            }
            tab(tabs+1, out);
            out << "}";
        
            tab(tabs+1, out);
            tab(tabs+1, out);
            out << "virtual void instanceResetUserInterface()";
            tab(tabs+1, out);
            out << "{";
            {
                FBCCPPCompiler<T> compiler;
                compiler.CompileBlock(this->fFactory->fResetUIBlock, tabs+2, out);
            }
            tab(tabs+1, out);
            out << "}";
        
            tab(tabs+1, out);
            out << "virtual void instanceClear()";
            tab(tabs+1, out);
            out << "{";
            {
                FBCCPPCompiler<T> compiler;
                compiler.CompileBlock(this->fFactory->fClearBlock, tabs+2, out);
            }
            tab(tabs+1, out);
            out << "}";
        
            tab(tabs+1, out);
            tab(tabs+1, out);
            out << "virtual mydsp* clone()";
            tab(tabs+1, out);
            out << "{";
                tab(tabs+2, out);
                out << "return new mydsp(); ";
            tab(tabs+1, out);
            out << "}";
        
            tab(tabs+1, out);
            tab(tabs+1, out);
            out << "virtual void metadata(Meta* m)";
            tab(tabs+1, out);
            out << "{";
            {
                FBCCPPCompiler<T> compiler;
                compiler.CompileBlock(this->fFactory->fMetaBlock, tabs+2, out);
            }
            tab(tabs+1, out);
            out << "}";
        
            tab(tabs+1, out);
            tab(tabs+1, out);
            out << "virtual void compute(int count, FAUSTFLOAT** inputs, FAUSTFLOAT** outputs)";
            tab(tabs+1, out);
            out << "{";
            {
                tab(tabs+2, out);
                out << "if (count == 0) return;  // Beware: compiled loop don't work with an index of 0";
                out << "fIntHeap[" << this->fFactory->fCountOffset << "] = count;";
                FBCCPPCompiler<T> compiler;
                compiler.CompileBlock(this->fFactory->fComputeBlock, tabs+2, out, false);
                compiler.CompileBlock(this->fFactory->fComputeDSPBlock, tabs+2, out);
            }
            tab(tabs+1, out);
            out << "}";
        tab(tabs, out);
        out << "};";
        tab(tabs, out);
    }
    
};

#endif
