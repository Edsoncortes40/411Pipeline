
#include "mips-small-pipe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/************************************************************/
int main(int argc, char *argv[]) {
  short i;
  char line[MAXLINELENGTH];
  state_t state;
  FILE *filePtr;

  if (argc != 2) {
    printf("error: usage: %s <machine-code file>\n", argv[0]);
    return 1;
  }

  memset(&state, 0, sizeof(state_t));

  state.pc = state.cycles = 0;
  state.IFID.instr = state.IDEX.instr = state.EXMEM.instr = state.MEMWB.instr =
      state.WBEND.instr = NOPINSTRUCTION; /* nop */

  /* read machine-code file into instruction/data memory (starting at address 0)
   */

  filePtr = fopen(argv[1], "r");
  if (filePtr == NULL) {
    printf("error: can't open file %s\n", argv[1]);
    perror("fopen");
    exit(1);
  }

  for (state.numMemory = 0; fgets(line, MAXLINELENGTH, filePtr) != NULL;
       state.numMemory++) {
    if (sscanf(line, "%x", &state.dataMem[state.numMemory]) != 1) {
      printf("error in reading address %d\n", state.numMemory);
      exit(1);
    }
    state.instrMem[state.numMemory] = state.dataMem[state.numMemory];
    printf("memory[%d]=%x\n", state.numMemory, state.dataMem[state.numMemory]);
  }

  printf("%d memory words\n", state.numMemory);

  printf("\tinstruction memory:\n");
  for (i = 0; i < state.numMemory; i++) {
    printf("\t\tinstrMem[ %d ] = ", i);
    printInstruction(state.instrMem[i]);
  }

  run(&state);

  return 0;
}
/************************************************************/

/************************************************************/
void run(Pstate state) {
  state_t new;
  int instruction;
  int dataMemIndex;
  int instructions = 0;
  int checkForward1;
  int checkForward2;
  int checkForward3;
  int tempA;
  int tempB;
  int lw_forwarding;
  
  memset(&new, 0, sizeof(state_t));

  for(; 1; instructions++) {

    printState(state);
    
    /* copy everything so all we have to do is make changes.
       (this is primarily for the memory and reg arrays) */

    memcpy(&new, state, sizeof(state_t));

    new.cycles++;

    /* --------------------- IF stage --------------------- */

    /* get instruction from copy state */
    instruction = new.instrMem[new.pc / 4];
    
    /* store instruction into IF to ID Register */
    new.IFID.instr = instruction;
    
    /* increment program counter */
    new.pc = new.pc + 4;

    /* update program counter in IF to ID register */
    /* check if taken first */
    if(opcode(new.IFID.instr) == BEQZ_OP && offset(instruction) < 1)
    {
      new.IFID.pcPlus1 = new.pc;
      new.pc = new.pc + offset(instruction);
    }
    else
    {
      new.IFID.pcPlus1 = new.pc;
    }
    
    /* --------------------- ID stage --------------------- */

    /* get instruction from IF to ID register */
    instruction = state->IFID.instr;
    new.IDEX.instr = instruction;

    
    /* store instruction settings to ID register */
    new.IDEX.readRegA = new.reg[field_r1(instruction)];
    new.IDEX.readRegB = new.reg[field_r2(instruction)];
    new.IDEX.offset = offset(instruction);
    new.IDEX.pcPlus1 = state->IFID.pcPlus1;

    /* check for forwarding from lw */
    lw_forwarding = state->IDEX.instr;

    if((field_r2(lw_forwarding) == field_r2(instruction) || field_r2(lw_forwarding) == field_r1(instruction)) &&
       opcode(instruction) != HALT_OP &&
       opcode(instruction) == REG_REG_OP &&
       opcode(state->IDEX.instr) == LW_OP)
      {
	/* update ID to EX Register */
	new.IDEX.instr = NOPINSTRUCTION;
	new.IDEX.offset = offset(NOPINSTRUCTION);
	new.IDEX.pcPlus1 = 0;
	new.IDEX.readRegA = 0;
	new.IDEX.readRegB = 0;
	
	/* update instruction to IF to ID instruction */
	instruction = state->IFID.instr;
	new.IFID.instr = instruction;
	new.pc = new.pc - 4;
	new.IFID.pcPlus1 = new.pc;
	
      }
    else if(opcode(lw_forwarding) == LW_OP &&
	    field_r2(lw_forwarding) == field_r1(instruction))
    {
      /* update ID to EX register */
      if(opcode(instruction) != HALT_OP)
      {
	new.IDEX.instr = NOPINSTRUCTION;
	new.IDEX.offset = offset(NOPINSTRUCTION);
	new.IDEX.pcPlus1 = 0;
	new.IDEX.readRegA = 0;
	new.IDEX.readRegB = 0;
      
	/*update instruction to IF to ID instruction */
	instruction = state->IFID.instr;
	new.IFID.instr = instruction;
	new.pc = new.pc - 4;
	new.IFID.pcPlus1 = new.pc;
      }
    }
    
    /* --------------------- EX stage --------------------- */

    /* get instruction from EX to MEM register */
    instruction = state->IDEX.instr;
    new.EXMEM.instr = instruction;
    
    /* decode instructions */

    /* check previous registers to see if there was forwarding to EX stage */
    checkForward1 = opcode(state->EXMEM.instr);
    checkForward2 = opcode(state->MEMWB.instr);
    checkForward3 = opcode(state->WBEND.instr);
    tempA = new.reg[field_r1(instruction)];
    tempB = new.reg[field_r2(instruction)];
    
    /* check WB to END register for forwarding */
    if(checkForward3 == ADDI_OP)
    {
      if(checkForward3 != opcode(NOPINSTRUCTION))
      {
	if(field_r1(instruction) == field_r2(state->WBEND.instr)
	   && field_r1(instruction) != 0)
	  {
	    tempA = state->WBEND.writeData;
	  }
	
	if(field_r2(instruction) == field_r2(state->WBEND.instr)
	   && field_r2(instruction != 0))
	  {
	    tempB = state->WBEND.writeData;
	  }
      }
    }
    else if(checkForward3 == LW_OP)
    {
      if(checkForward3 != opcode(NOPINSTRUCTION))
      {
	if(field_r1(instruction) == field_r2(state->WBEND.instr)
	   && field_r1(instruction) != 0)
	  {
	    tempA = state->WBEND.writeData;
	  }
	
	if(field_r2(instruction) == field_r2(state->WBEND.instr)
	   && field_r2(instruction) != 0)
	  {
	    tempB = state->WBEND.writeData;
	  }
      }
    }
    else if(checkForward3 == SW_OP)
    {
      if(field_r2(instruction) == field_r2(state->WBEND.instr)
	 && checkForward3 != opcode(NOPINSTRUCTION)
	 && field_r2(instruction) != 0)
      {
	tempB = state->EXMEM.aluResult;
      }
    }
    else if(checkForward3 == REG_REG_OP)
    {
      if(checkForward3 != opcode(NOPINSTRUCTION))
      {	
	if(field_r1(instruction) == field_r3(state->WBEND.instr)
	   && field_r1(instruction) != 0)
	  {
	    tempA = state->WBEND.writeData;
	  }
	
	if(field_r2(instruction) == field_r3(state->WBEND.instr)
	   && field_r2(instruction != 0))
	  {
	    tempB = state->WBEND.writeData;
	  }
      }
    }
    else if(checkForward3 == HALT_OP)
    {
      /* printf("Halt in progress"); */
    }
    else if(checkForward3 == BEQZ_OP)
    {
      if(checkForward3 != opcode(NOPINSTRUCTION))
      {
	if(field_r1(instruction) == field_r2(state->WBEND.instr)
	   && field_r1(instruction) != 0)
	{
	  tempA = state->WBEND.aluResult;
	}

	if(field_r2(instruction) == field_r2(state->WBEND.instr)
	   && field_r2(instruction) != 0)
	{
	  tempB = state->WBEND.aluResult;
        }
      }
    }
    else
    {
      /* printf("No forwarding in EX to MEM register!"); */
    }
    
    /* check MEM to WB register for forwarding */
    if(checkForward2 == ADDI_OP)
      {
	if(checkForward2 != opcode(NOPINSTRUCTION))
	{
	  if(field_r1(instruction) == field_r2(state->MEMWB.instr)
	     && field_r1(instruction) != 0)
	    {
	      tempA = state->MEMWB.writeData;
	    }
	  
	  if(field_r2(instruction) == field_r2(state->MEMWB.instr)
	     && field_r2(instruction) != 0)
	    {
	      tempB = state->MEMWB.writeData;
	    }
	}
    }
    else if(checkForward2 == LW_OP)
    {
      if(checkForward2 != opcode(NOPINSTRUCTION))
      {
	if(field_r1(instruction) == field_r2(state->MEMWB.instr)
	   && field_r1(instruction) != 0)
	{
	   tempA = state->MEMWB.writeData;
	}
	
	if(field_r2(instruction) == field_r2(state->MEMWB.instr)
	   && field_r2(instruction) != 0)
	  {
	    tempB = state->MEMWB.writeData;
	  }
      }
    }
    else if(checkForward2 == SW_OP)
    {
	  /* ??? */
    }
    else if(checkForward2 == REG_REG_OP)
    {
      if(field_r1(instruction) == field_r3(state->MEMWB.instr)
	 && field_r1(instruction) != 0)
      {
	tempA = state->MEMWB.writeData;
      }

      if(field_r2(instruction) == field_r3(state->MEMWB.instr)
	 && field_r2(instruction) != 0)
      {
	tempB = state->MEMWB.writeData;
      }
    }
    else if(checkForward2 == HALT_OP)
    {
      /* printf("Halt in progress"); */
    }
    else if(checkForward2 == BEQZ_OP)
    {
      if(checkForward2 != opcode(NOPINSTRUCTION))
      {
	if(field_r1(instruction) == field_r2(state->MEMWB.instr)
	   && field_r1(instruction) != 0)
	{
	  tempA = state->MEMWB.writeData;
	}

	if(field_r2(instruction) == field_r2(state->MEMWB.instr)
	   && field_r1(instruction) != 0)
	{
	  tempB = state->MEMWB.writeData;
	}
      }
    }
    else
    {
      /* printf("no Fowarding in MEM to WB register!"); */
    }
    
    /* check EX to MEM register for forwarding */
    if(checkForward1 == ADDI_OP)
    {
      if(checkForward1 != opcode(NOPINSTRUCTION))
      {
	if(field_r1(instruction) == field_r2(state->EXMEM.instr)
	   && field_r1(instruction) != 0)
	  {
	    tempA = state->EXMEM.aluResult;
	  }
	
	if(field_r2(instruction) == field_r2(state->EXMEM.instr)
	   && field_r2(instruction) != 0)
	  {
	    tempB = state->EXMEM.aluResult;
	  }
      }
    }
    else if(checkForward1 == LW_OP)
    {
      if(checkForward1 != opcode(NOPINSTRUCTION))
      {
	if(field_r1(instruction) == field_r2(state->EXMEM.instr)
	   && field_r1(instruction) != 0)
	  {
	    tempA = state->EXMEM.aluResult;
	  }
	
	if(field_r2(instruction) == field_r2(state->EXMEM.instr)
	   && field_r2(instruction) != 0)
	  {
	    tempB = state->EXMEM.aluResult;
	  }
      }
    }
    else if(checkForward1 == SW_OP)
    {
      /* ??? */
    }
    else if(checkForward1 == REG_REG_OP)
    {
      if(checkForward1 != opcode(NOPINSTRUCTION))
      {
	if(field_r1(instruction) == field_r3(state->EXMEM.instr)
	   && field_r1(instruction) != 0))
	  {
	    tempA = state->EXMEM.aluResult;
	  }
	
	if(field_r2(instruction) == field_r3(state->WBEND.instr)
	   && field_r2(instruction) != 0)
	  {
	    tempB = state->EXMEM.aluResult;
	  }
      }
    }
    else if(checkForward1 == HALT_OP)
    {
      /* printf("halt in progress!"); */
    }
    else if(checkForward1 == BEQZ_OP)
    {
      if(checkForward1 != opcode(NOPINSTRUCTION))
      {
	if(field_r1(instruction) == field_r2(state->EXMEM.instr))
	{
	  tempA = state->EXMEM.writeData;
	}

	if(field_r2(instruction) == field_r2(state->EXMEM.instr))
	{
	  tempB = state->EXMEM.writeData;
	}
      }
    }
    else
    {
      /* printf("no forwarding in WB to END register!"); */
    }
    
    /* execute instructions */
    if(opcode(instruction) == ADDI_OP)
    {
      new.EXMEM.aluResult = tempA + offset(instruction);
      new.EXMEM.readRegB = state->IDEX.readRegB;
    }
    else if(opcode(instruction) == LW_OP)
    {
      new.EXMEM.aluResult = tempA + field_imm(instruction);
      new.EXMEM.readRegB = new.reg[field_r2(instruction)];
    }
    else if(opcode(instruction) == SW_OP)
    {
      new.EXMEM.readRegB = tempB;
      new.EXMEM.aluResult = tempA + field_imm(instruction);
    }
    else if(opcode(instruction) == REG_REG_OP)
    {
      /* check what kind of REG to REG instruction this is */
      if(instruction == NOPINSTRUCTION)
      {
	new.EXMEM.aluResult = 0;
	new.EXMEM.readRegB = 0;
      }
      else if(func(instruction) == ADD_FUNC)
      {
	new.EXMEM.aluResult = tempA + tempB;
	new.EXMEM.readRegB = tempB;
      }
      else if(func(instruction) == SUB_FUNC)
      {
	new.EXMEM.aluResult = tempA - tempB;
	new.EXMEM.readRegB = tempB;
      }
      else if(func(instruction) == AND_FUNC)
      {
	new.EXMEM.aluResult = tempA & tempB;
	new.EXMEM.readRegB = tempB;
      }
      else if(func(instruction) == OR_FUNC)
      {
	new.EXMEM.aluResult = tempA | tempA;
	new.EXMEM.readRegB = tempB;
      }
      else if(func(instruction) == SLL_FUNC)
      {
	new.EXMEM.aluResult = tempA << tempB;
	new.EXMEM.readRegB = tempB;
      }
      else if(func(instruction) == SRL_FUNC)
      {
	new.EXMEM.aluResult = (unsigned int) tempA >> tempB;
	new.EXMEM.readRegB = tempB;
      }
      else
      {
	/* printf("error in reg to reg function decode!"); */
      }
    }
    else if(opcode(instruction) == BEQZ_OP)
    {
      if((state->IDEX.offset < 0 && tempA !=  0)
	 || (state->IDEX.offset > 0 && tempA == 0))
      {
	new.IFID.pcPlus1 = 0;
	new.IDEX.readRegA = 0;
	new.IDEX.pcPlus1 = 0;
	new.IDEX.offset = 32;
	new.IDEX.readRegB = 0;
	new.IFID.instr = NOPINSTRUCTION;
	new.IDEX.instr = NOPINSTRUCTION;
	new.EXMEM.aluResult = state->IDEX.pcPlus1 + offset(instruction);
	new.EXMEM.readRegB = new.reg[field_r2(instruction)];
	new.pc = state->IDEX.pcPlus1 + state->IDEX.offset;
      }
      else
      {
	new.EXMEM.aluResult = state->IDEX.pcPlus1 + offset(instruction);
	new.EXMEM.readRegB = new.reg[field_r2(instruction)];
      }	  
    }
    else
    {
      new.EXMEM.aluResult = 0;
      new.EXMEM.readRegB = 0;
    }
    /* --------------------- MEM stage --------------------- */
    
    /* fetch instuction from MEM to WB register */
    instruction = state->EXMEM.instr;
    new.MEMWB.instr = instruction;

    if(opcode(instruction) == ADDI_OP)
    {
      new.MEMWB.writeData = state->EXMEM.aluResult;
    }
    else if(opcode(instruction) == LW_OP)
    {
      new.MEMWB.writeData = state->dataMem[state->EXMEM.aluResult / 4];
    }
    else if(opcode(instruction) == SW_OP)
    {
      new.MEMWB.writeData = state->EXMEM.readRegB;
      dataMemIndex = new.reg[field_r1(instruction)] + field_imm(instruction);
      new.dataMem[dataMemIndex / 4] = new.MEMWB.writeData;
    }
    else if(opcode(instruction) == REG_REG_OP)
    {
      new.MEMWB.writeData = state->EXMEM.aluResult;
    }
    else if(opcode(instruction) == HALT_OP)
    {
      new.MEMWB.writeData = 0;
    }
    else if(opcode(instruction) == BEQZ_OP)
    {
      new.MEMWB.writeData = state->EXMEM.aluResult;
    }
    else
    {
      /* printf("error in MEM stage!"); */
    }
    /* --------------------- WB stage --------------------- */
    instruction = state->MEMWB.instr;
    new.WBEND.instr = instruction;
    
      if(opcode(instruction) == ADDI_OP)
      {
        new.WBEND.writeData = state->MEMWB.writeData;
	new.reg[field_r2(instruction)] = new.WBEND.writeData;
      }
      else if(opcode(instruction) == LW_OP)
      {
	new.WBEND.writeData = state->MEMWB.writeData;
	new.reg[field_r2(instruction)] = new.WBEND.writeData;
      }
      else if(opcode(instruction) == SW_OP)
      {
	new.WBEND.writeData = state->MEMWB.writeData;
      }
      else if(opcode(instruction) == REG_REG_OP)
      {
	new.reg[field_r3(instruction)] = state->MEMWB.writeData;
	new.WBEND.writeData = state->MEMWB.writeData;
      }
      else if(opcode(instruction) == HALT_OP)
      {
	printf("machine halted\n");
	printf("total of %d cycles executed\n", state->cycles);
	exit(0);
      }
      else if(opcode(instruction) == BEQZ_OP)
      {
	new.WBEND.writeData = state->MEMWB.writeData;
      }
      else
      {
	/* printf("error in reading opcode in WB stage"); */
      }

      tempA = 0;
      tempB = 0;


      
     /* --------------------- end stage --------------------- */
      
    /* transfer new state into current state */
    memcpy(state, &new, sizeof(state_t));
  }
}
/************************************************************/

/************************************************************/
int opcode(int instruction) { return (instruction >> OP_SHIFT) & OP_MASK; }
/************************************************************/

/************************************************************/
int func(int instruction) { return (instruction & FUNC_MASK); }
/************************************************************/

/************************************************************/
int field_r1(int instruction) { return (instruction >> R1_SHIFT) & REG_MASK; }
/************************************************************/

/************************************************************/
int field_r2(int instruction) { return (instruction >> R2_SHIFT) & REG_MASK; }
/************************************************************/

/************************************************************/
int field_r3(int instruction) { return (instruction >> R3_SHIFT) & REG_MASK; }
/************************************************************/

/************************************************************/
int field_imm(int instruction) { return (instruction & IMMEDIATE_MASK); }
/************************************************************/

/************************************************************/
int offset(int instruction) {
  /* only used for lw, sw, beqz */
  return convertNum(field_imm(instruction));
}
/************************************************************/

/************************************************************/
int convertNum(int num) {
  /* convert a 16 bit number into a 32-bit Sun number */
  if (num & 0x8000) {
    num -= 65536;
  }
  return (num);
}
/************************************************************/

/************************************************************/
void printState(Pstate state) {
  short i;
  printf("@@@\nstate before cycle %d starts\n", state->cycles);
  printf("\tpc %d\n", state->pc);

  printf("\tdata memory:\n");
  for (i = 0; i < state->numMemory; i++) {
    printf("\t\tdataMem[ %d ] %d\n", i, state->dataMem[i]);
  }
  printf("\tregisters:\n");
  for (i = 0; i < NUMREGS; i++) {
    printf("\t\treg[ %d ] %d\n", i, state->reg[i]);
  }
  printf("\tIFID:\n");
  printf("\t\tinstruction ");
  printInstruction(state->IFID.instr);
  printf("\t\tpcPlus1 %d\n", state->IFID.pcPlus1);
  printf("\tIDEX:\n");
  printf("\t\tinstruction ");
  printInstruction(state->IDEX.instr);
  printf("\t\tpcPlus1 %d\n", state->IDEX.pcPlus1);
  printf("\t\treadRegA %d\n", state->IDEX.readRegA);
  printf("\t\treadRegB %d\n", state->IDEX.readRegB);
  printf("\t\toffset %d\n", state->IDEX.offset);
  printf("\tEXMEM:\n");
  printf("\t\tinstruction ");
  printInstruction(state->EXMEM.instr);
  printf("\t\taluResult %d\n", state->EXMEM.aluResult);
  printf("\t\treadRegB %d\n", state->EXMEM.readRegB);
  printf("\tMEMWB:\n");
  printf("\t\tinstruction ");
  printInstruction(state->MEMWB.instr);
  printf("\t\twriteData %d\n", state->MEMWB.writeData);
  printf("\tWBEND:\n");
  printf("\t\tinstruction ");
  printInstruction(state->WBEND.instr);
  printf("\t\twriteData %d\n", state->WBEND.writeData);
}
/************************************************************/

/************************************************************/
void printInstruction(int instr) {

  if (opcode(instr) == REG_REG_OP) {

    if (func(instr) == ADD_FUNC) {
      print_rtype(instr, "add");
    } else if (func(instr) == SLL_FUNC) {
      print_rtype(instr, "sll");
    } else if (func(instr) == SRL_FUNC) {
      print_rtype(instr, "srl");
    } else if (func(instr) == SUB_FUNC) {
      print_rtype(instr, "sub");
    } else if (func(instr) == AND_FUNC) {
      print_rtype(instr, "and");
    } else if (func(instr) == OR_FUNC) {
      print_rtype(instr, "or");
    } else {
      printf("data: %d\n", instr);
    }

  } else if (opcode(instr) == ADDI_OP) {
    print_itype(instr, "addi");
  } else if (opcode(instr) == LW_OP) {
    print_itype(instr, "lw");
  } else if (opcode(instr) == SW_OP) {
    print_itype(instr, "sw");
  } else if (opcode(instr) == BEQZ_OP) {
    print_itype(instr, "beqz");
  } else if (opcode(instr) == HALT_OP) {
    printf("halt\n");
  } else {
    printf("data: %d\n", instr);
  }
}
/************************************************************/

/************************************************************/
void print_rtype(int instr, const char *name) {
  printf("%s %d %d %d\n", name, field_r3(instr), field_r1(instr),
         field_r2(instr));
}
/************************************************************/

/************************************************************/
void print_itype(int instr, const char *name) {
  printf("%s %d %d %d\n", name, field_r2(instr), field_r1(instr),
         offset(instr));
}
/************************************************************/
