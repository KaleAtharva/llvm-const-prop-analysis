#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/FileSystem.h"
#include <filesystem>
#include <queue> 
#include <unordered_map>

using namespace llvm;

namespace {
  enum lattice{
    TOP,CONSTANT,BOTTOM
  };

  enum Mode{
    PRINT,ANALYSIS
  };

  std::pair<lattice,int> Meet(std::pair<lattice,int> l1,std::pair<lattice,int> l2){
    if((l1.first==lattice::CONSTANT && l2.first==lattice::CONSTANT) && l1.second==l2.second){
      return {lattice::CONSTANT,l1.second};
    }else if((l1.first==lattice::CONSTANT && l2.first==lattice::CONSTANT) && l1.second!=l2.second){
      return {lattice::BOTTOM,0};
    }else if(l1.first==lattice::BOTTOM || l2.first==lattice::BOTTOM){
      return {lattice::BOTTOM,0};
    }else if(l1.first==lattice::TOP && l2.first==lattice::TOP){
      return {lattice::TOP,0};
    }else if((l1.first==lattice::TOP && l2.first==lattice::CONSTANT) || (l1.first==lattice::CONSTANT && l2.first==lattice::TOP)){
      return {lattice::BOTTOM,0};
    }
  }

  int evaluate(unsigned opcode, int v1, int v2){
    switch(opcode){
      case Instruction::Add : return v1+v2;
      case Instruction::Sub : return v1-v2;
      case Instruction::Mul : return v1*v2;
      case Instruction::SDiv : return v1/v2;
    }
  }

  bool icmpRet(ICmpInst* ICMP, int v1, int v2){
    bool res;
    switch (ICMP->getPredicate()) {
        case ICmpInst::ICMP_EQ:  res = (v1 == v2); break;
        case ICmpInst::ICMP_NE:  res = (v1 != v2); break;
        case ICmpInst::ICMP_SLT: res = (v1 < v2); break;
        case ICmpInst::ICMP_SLE: res = (v1 <= v2); break;
        case ICmpInst::ICMP_SGT: res = (v1 > v2); break;
        case ICmpInst::ICMP_SGE: res = (v1 >= v2); break;
    }
    return res;
  }

  std::queue<BasicBlock*> q;
  using Env = std::unordered_map<std::string,std::pair<lattice,int>>;
  
  std::unordered_map<BasicBlock*,Env> inMap;
  std::unordered_map<BasicBlock*,Env> outMap;
  std::unique_ptr<raw_fd_ostream> OutFile;
  std::error_code EC;

  std::string getValueName(Value *V) {
    std::string s;
    raw_string_ostream rs(s);
    V->printAsOperand(rs, false);
    return rs.str();
  }

  void printOperands(std::string operandName, Env curr){
    if(curr[operandName].first==lattice::BOTTOM) 
    *OutFile<<operandName<<" = "<<"BOTTOM "; 
    else if(curr[operandName].first == lattice::TOP)
    *OutFile<<operandName<<" = "<<"TOP ";
    else 
    *OutFile<<operandName<<" = "<<curr[operandName].second<<" ";
  }

  void effect(unsigned opcode, std::unordered_map<std::string,std::pair<lattice,int>> &curr, Instruction &I, BasicBlock *BB, Mode mode){
    if(opcode == Instruction::Alloca){
      std::string var;
      raw_string_ostream rs(var);
      I.printAsOperand(rs, false);
      curr[var] = { lattice::TOP, 0 };

      if(mode == Mode::PRINT){
        *OutFile<<" --> ";
        *OutFile<<var<<" = "<<"TOP ";
      }
    }else if(opcode == Instruction::Store){
      Value* operand1 = I.getOperand(0);
      Value* operand2 = I.getOperand(1);

      std::string src = getValueName(operand1);
      std::string dst = getValueName(operand2);

      if(mode == Mode::PRINT) *OutFile<<" --> ";

      if (const ConstantInt* CI = dyn_cast<llvm::ConstantInt>(operand1)) {
        int64_t intValue = CI->getSExtValue(); 
        curr[dst] = {lattice::CONSTANT,intValue};
      }else{
        auto it = curr.find(src);
        curr[dst] = (it == curr.end())
                      ? std::make_pair(lattice::TOP, 0)
                      : it->second;

        if(mode == Mode::PRINT){
          printOperands(src,curr);
        }
      }

      if(mode == Mode::PRINT){
        printOperands(dst,curr);
      }
    }else if(opcode == Instruction::Call){
      CallInst *CI = cast<CallInst>(&I);
      Function *Callee = CI->getCalledFunction();

      if(mode==Mode::PRINT) *OutFile<<" --> ";

      Type *RetType = CI->getType();
      if(!RetType->isVoidTy()){
        std::string lhs;
        raw_string_ostream rs(lhs);
        I.printAsOperand(rs, false);

        
        curr[lhs] = { lattice::TOP, 0 };

        if(mode == Mode::PRINT){
          *OutFile<<lhs<<" = "<<"TOP ";
        }
      }
      
      if(Callee->getName()=="printf"){
        for(unsigned i=1;i<CI->arg_size();i++){
          Value* Arg = CI->getArgOperand(i);
          std::string arg;
          raw_string_ostream rs(arg);
          Arg->printAsOperand(rs,false);
          
          if(mode == Mode::PRINT){
            if (ConstantInt *C = dyn_cast<ConstantInt>(Arg)) {
            
            }else{
              printOperands(arg, curr);
            }
          }
        }

        return;
      }
      else if(Callee->getName().contains("scanf")){
        for(unsigned i=1;i<CI->arg_size();i++){
          Value* Arg = CI->getArgOperand(i);
          std::string arg;
          raw_string_ostream rs(arg);
          Arg->printAsOperand(rs,false);
          curr[arg] = {lattice::BOTTOM,0};

          if(mode == Mode::PRINT){ 
            *OutFile<<arg<<" = "<<"BOTTOM ";
          }
        }
      }
      else{
        for(unsigned i=0;i<CI->arg_size();i++){
          Value* Arg = CI->getArgOperand(i);
          std::string arg;
          raw_string_ostream rs(arg);
          Arg->printAsOperand(rs,false);
          
          if(mode == Mode::PRINT){
            if (ConstantInt *C = dyn_cast<ConstantInt>(Arg)) {
            
            }else{
              printOperands(arg, curr);
            }
          }
        }
      }
    }else if(opcode == Instruction::Load){
      LoadInst* LI = cast<LoadInst>(&I);
      
      std::string lhs;
      raw_string_ostream rs1(lhs);
      I.printAsOperand(rs1, false);

      Value* Operand = LI->getPointerOperand();
      std::string op;
      raw_string_ostream rs(op);
      Operand->printAsOperand(rs,false);

      auto it = curr.find(op);
      curr[lhs] = (it == curr.end())
                    ? std::make_pair(lattice::BOTTOM, 0)
                    : it->second;

      if(mode == Mode::PRINT){
        *OutFile<<" --> "; 
        printOperands(lhs, curr);
        *OutFile<<" "; 
        printOperands(op, curr);
      }
    }else if(I.isBinaryOp()){
      BinaryOperator* BO = cast<BinaryOperator>(&I);
      Value* operand0 = BO->getOperand(0);
      Value* operand1 = BO->getOperand(1);

      std::string lhs;
      raw_string_ostream rs(lhs);
      I.printAsOperand(rs, false);

      std::pair<lattice,int> Op1,Op0;

      if(mode == Mode::PRINT) *OutFile<<" --> ";

      if(ConstantInt* CI0 = dyn_cast<llvm::ConstantInt>(operand0)){
        if(ConstantInt* CI1 = dyn_cast<llvm::ConstantInt>(operand1)){
          int64_t value0 = CI0->getSExtValue();
          int64_t value1 = CI1->getSExtValue();

          Op0 = {lattice::CONSTANT,value0};
          Op1 = {lattice::CONSTANT,value1};

          if(Op0.first==lattice::BOTTOM || Op1.first==lattice::BOTTOM){
            curr[lhs] = {lattice::BOTTOM,0};
          }else if(Op0.first==lattice::TOP || Op1.first==lattice::TOP){
            curr[lhs] = {lattice::TOP,0};
          }else{
            if(opcode == Instruction::SDiv){
              if(Op0.second==0) curr[lhs] = {lattice::BOTTOM,0};
              else if(Op0.second == -1 && Op1.second == INT_MIN) curr[lhs] = {lattice::BOTTOM,0};
              else {
                int result = evaluate(opcode,Op1.second,Op0.second);
                curr[lhs] = {lattice::CONSTANT,result};
              }
            }else{
              int result = evaluate(opcode,Op0.second,Op1.second);
              curr[lhs] = {lattice::CONSTANT,result};
            }
          }

          if(mode==Mode::PRINT){
            printOperands(lhs, curr);
          }
          }         
      }
      else if(ConstantInt* CI0 = dyn_cast<llvm::ConstantInt>(operand0)){
        int64_t value0 = CI0->getSExtValue();
        std::string oper1;
        raw_string_ostream rs1(oper1);
        operand1->printAsOperand(rs1, false);

        Op1 = curr[oper1];
        Op0 = {lattice::CONSTANT,value0};

        if(Op0.first==lattice::BOTTOM || Op1.first==lattice::BOTTOM){
          curr[lhs] = {lattice::BOTTOM,0};
        }else if(Op0.first==lattice::TOP || Op1.first==lattice::TOP){
          curr[lhs] = {lattice::TOP,0};
        }else{
          if(opcode == Instruction::SDiv){
            if(Op0.second==0) curr[lhs] = {lattice::BOTTOM,0};
            else if(Op0.second == -1 && Op1.second == INT_MIN) curr[lhs] = {lattice::BOTTOM,0};
            else {
              int result = evaluate(opcode,Op1.second,Op0.second);
              curr[lhs] = {lattice::CONSTANT,result};
            }
          }else{
            int result = evaluate(opcode,Op0.second,Op1.second);
            curr[lhs] = {lattice::CONSTANT,result};
          }
        }

        if(mode==Mode::PRINT){
          printOperands(lhs, curr);
          printOperands(oper1, curr);
        }
      }
      else if(ConstantInt* CI1 = dyn_cast<llvm::ConstantInt>(operand1)){
        int64_t value1 = CI1->getSExtValue();
        
        std::string oper0;
        raw_string_ostream rs1(oper0);
        operand0->printAsOperand(rs1, false);

        Op0 = curr[oper0];
        Op1 = {lattice::CONSTANT,value1};

        if(Op0.first==lattice::BOTTOM || Op1.first==lattice::BOTTOM){
          curr[lhs] = {lattice::BOTTOM,0};
        }else if(Op0.first==lattice::TOP || Op1.first==lattice::TOP){
          curr[lhs] = {lattice::TOP,0};
        }else{
          if(opcode == Instruction::SDiv){
            if(Op0.second==0) curr[lhs] = {lattice::BOTTOM,0};
            else if(Op0.second == -1 && Op1.second == INT_MIN) curr[lhs] = {lattice::BOTTOM,0};
            else {
              int result = evaluate(opcode,Op1.second,Op0.second);
              curr[lhs] = {lattice::CONSTANT,result};
            }
          }else{
            int result = evaluate(opcode,Op0.second,Op1.second);
            curr[lhs] = {lattice::CONSTANT,result};
          }
        }

        if(mode==Mode::PRINT){
          printOperands(lhs, curr);
          printOperands(oper0, curr);
        }
      }else{
        std::string oper0;
        raw_string_ostream rs1(oper0);
        operand0->printAsOperand(rs1, false);

        std::string oper1;
        raw_string_ostream rs2(oper1);
        operand1->printAsOperand(rs2, false);

        Op0 = curr[oper0];
        Op1 = curr[oper1];

        if(Op0.first==lattice::BOTTOM || Op1.first==lattice::BOTTOM){
          curr[lhs] = {lattice::BOTTOM,0};
        }else if(Op0.first==lattice::TOP || Op1.first==lattice::TOP){
          curr[lhs] = {lattice::TOP,0};
        }else{
          if(opcode == Instruction::SDiv){
            if(Op0.second==0) curr[lhs] = {lattice::BOTTOM,0};
            else if(Op0.second == -1 && Op1.second == INT_MIN) curr[lhs] = {lattice::BOTTOM,0};
            else {
              int result = evaluate(opcode,Op1.second,Op0.second);
              curr[lhs] = {lattice::CONSTANT,result};
            }
          }else{
            int result = evaluate(opcode,Op0.second,Op1.second);
            curr[lhs] = {lattice::CONSTANT,result};
          }
        }

        if(mode==Mode::PRINT){
          printOperands(lhs, curr);
          printOperands(oper0, curr);
          *OutFile<<" ";
          printOperands(oper1, curr);
        }
      }
    }else if(opcode == Instruction::ICmp){
      ICmpInst* ICMP = cast<ICmpInst>(&I);

      Value *LHS = ICMP->getOperand(0);
      Value *RHS = ICMP->getOperand(1);
      //ICMP::Predicate Pred = ICMP->getPredicate();

      std::string operand;
      raw_string_ostream rs2(operand);
      I.printAsOperand(rs2, false);

      if(mode==Mode::PRINT) *OutFile<<" --> ";
      if(ConstantInt* CI0 = dyn_cast<llvm::ConstantInt>(LHS)){
        if(ConstantInt* CI1 = dyn_cast<llvm::ConstantInt>(RHS)){
          int64_t value0 = CI0->getSExtValue();
          int64_t value1 = CI1->getSExtValue();
          bool res = icmpRet(ICMP,value0,value1);
          
          curr[operand] = { lattice::CONSTANT, res ? 1 : 0 };

          if(mode==Mode::PRINT){
            printOperands(operand ,curr);
          }
        }
      }else if(ConstantInt* CI0 = dyn_cast<llvm::ConstantInt>(RHS)){
        int64_t value0 = CI0->getSExtValue();
        std::string lhs;
        raw_string_ostream rs(lhs);
        LHS->printAsOperand(rs, false);

        if(curr[lhs].first==lattice::TOP){
          curr[operand] = { lattice::TOP, 0 };
        }else if(curr[lhs].first==lattice::BOTTOM){
          curr[operand] = { lattice::BOTTOM, 0 };
        }else{
          bool res = icmpRet(ICMP,curr[lhs].second,value0);
          curr[operand] = { lattice::CONSTANT, res ? 1 : 0 };
        }

        if(mode==Mode::PRINT){
          printOperands(operand, curr);
          printOperands(lhs, curr);
        }
      }else if(ConstantInt* CI0 = dyn_cast<llvm::ConstantInt>(LHS)){
        int64_t value0 = CI0->getSExtValue();
        std::string rhs;
        raw_string_ostream rs(rhs);
        RHS->printAsOperand(rs, false);

        if(curr[rhs].first==lattice::TOP){
          curr[operand] = { lattice::TOP, 0 };
        }else if(curr[rhs].first==lattice::BOTTOM){
          curr[operand] = { lattice::BOTTOM, 0 };
        }else{
          bool res = icmpRet(ICMP,value0,curr[rhs].second);
          curr[operand] = { lattice::CONSTANT, res ? 1 : 0 };
        }

        if(mode==Mode::PRINT){
          printOperands(operand, curr);
          printOperands(rhs, curr);
        }
      }else{
        std::string lhs;
        raw_string_ostream rs(lhs);
        LHS->printAsOperand(rs, false);

        std::string rhs;
        raw_string_ostream rs1(rhs);
        RHS->printAsOperand(rs1, false);

        if(curr[rhs].first==lattice::TOP || curr[lhs].first==lattice::TOP){
          curr[operand] = { lattice::TOP, 0 };
        }else if(curr[rhs].first==lattice::BOTTOM || curr[lhs].first==lattice::TOP){
          curr[operand] = { lattice::BOTTOM, 0 };
        }else{
          bool res = icmpRet(ICMP,curr[lhs].second,curr[rhs].second);
          curr[operand] = { lattice::CONSTANT, res ? 1 : 0 };
        }

        if(mode==Mode::PRINT){
          printOperands(operand, curr);
          printOperands(lhs, curr);
          printOperands(rhs, curr);
        }
      }
    }else if(opcode == Instruction::Br && mode==Mode::PRINT){
      BranchInst* BR = cast<BranchInst>(&I);
      if(BR->isConditional()){
        *OutFile<<" --> ";
        Value* Conditional = BR->getOperand(0);
        std::string cond;
        raw_string_ostream rs(cond);
        Conditional->printAsOperand(rs, false);

        printOperands(cond, curr);
      }
    }else if(opcode == Instruction::Ret && mode==Mode::PRINT){
      ReturnInst* RetInst = cast<ReturnInst>(&I);
      Value *returnValue = RetInst->getReturnValue();
      std::string ret;
      raw_string_ostream rs(ret);
      returnValue->printAsOperand(rs, false);
      
      if (ConstantInt *CI = dyn_cast<ConstantInt>(returnValue)) {
      
      }else{
        *OutFile<<" --> ";
        printOperands(ret, curr);
      }
    }
  }

  void printFileOutput(Function &F){
    for(BasicBlock &BB : F){
      *OutFile<<"--------------------"<<"\n";

      Env curr = inMap[&BB];

      for(Instruction &I : BB){
        I.print(*OutFile);
        unsigned opcode = I.getOpcode();
        
        effect(opcode, curr, I, &BB, Mode::PRINT);

        *OutFile<<"\n";
      }
    }
    OutFile->flush();
  }
  bool fi = true;

struct constant_p : public FunctionPass {
  static char ID;
  constant_p() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {

    inMap.clear();
    outMap.clear();
    while(!q.empty()) q.pop();

    Module *M = F.getParent();
    std::string Src = M->getModuleIdentifier();
    std::filesystem::path p(Src);
    std::string fileName = p.stem().string();
    // write your code here
    if(fi){
      llvm::sys::fs::remove("./output/"+fileName+".txt");
      fi=false;
    }
    OutFile = std::make_unique<raw_fd_ostream>("./output/"+fileName+".txt", EC, sys::fs::OF_Append);
    *OutFile << F.getName();

    *OutFile << "\n";


    BasicBlock *EntryBlock = &F.getEntryBlock();
    q.push(EntryBlock);
    while(!q.empty()){
      BasicBlock *BB = q.front();

      inMap[BB].clear();
      bool firstPred = true;
      for(BasicBlock *pred : predecessors(BB)){
        if(firstPred){
          inMap[BB] = outMap[pred];
          firstPred = false;
        }else{
          for(const auto& pair : outMap[pred]){
            if(inMap[BB].count(pair.first)){
              inMap[BB][pair.first] = Meet(inMap[BB][pair.first],outMap[pred][pair.first]);
            }else{
              inMap[BB][pair.first] = outMap[pred][pair.first];
            }
          }
        }
      }

      std::unordered_map<std::string,std::pair<lattice,int>> curr;  
      curr = inMap[BB];
      for(Instruction &I : *BB){
        unsigned opcode = I.getOpcode();
        effect(opcode,curr,I,BB,Mode::ANALYSIS);
      }
      q.pop();
      if(outMap[BB]!=curr){
        outMap[BB] = curr;
        for(BasicBlock *succ : successors(BB)){
          q.push(succ);
        }
      }
    }
    printFileOutput(F);
    *OutFile << "\n\n";

    return false;
  }
}; // end of struct constant_p
}  // end of anonymous namespace

char constant_p::ID = 0;
static RegisterPass<constant_p> X("libCP_given", "Constant Propagation Pass for Assignment",
                             false /* Only looks at CFG */,
                             false /* Analysis Pass */);