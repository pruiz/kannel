/*
 *
 * wsopt.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Optimizations for the WMLScript symbolic assembler.
 *
 */

#include <wsint.h>
#include <wsasm.h>

/* TODO: liveness analyzation */

/********************* Optimization functions ***************************/

static WsBool
opt_jumps_to_jumps(WsCompilerPtr compiler)
{
  WsAsmIns *i;
  WsBool change = WS_TRUE;
  unsigned int count = 0;

  ws_info(compiler, "optimize: jumps to jumps");

  while (change)
    {
      count++;
      change = WS_FALSE;

      for (i = compiler->asm_head; i; i = i->next)
	{
	  WsAsmIns *j;

	  if (!WS_ASM_P_BRANCH(i))
	    continue;

	  /* Find the next instruction following the label. */
	  for (j = i->ws_label; j && j->type == WS_ASM_P_LABEL; j = j->next)
	    ;

	  if (j == NULL || j->type != WS_ASM_P_JUMP)
	    /* Can't optimize this case. */
	    continue;

	  /* We can optimize the jump `i' directly to the label of
             `j'.  We must remember to update the reference counts
             too. */

	  i->ws_label->ws_label_refcount--;
	  j->ws_label->ws_label_refcount++;

	  i->ws_label = j->ws_label;
	  change = WS_TRUE;
	}
    }

  return count > 1;
}


static WsBool
opt_jumps_to_next_instruction(WsCompilerPtr compiler)
{
  WsAsmIns *i;
  WsBool change = WS_FALSE;

  ws_info(compiler, "optimize: jumps to next instruction");

  for (i = compiler->asm_head; i; i = i->next)
    {
      WsAsmIns *j;

      if (i->type != WS_ASM_P_JUMP)
	continue;

      for (j = i->next;
	   j && j->type == WS_ASM_P_LABEL && i->ws_label != j;
	   j = j->next)
	;

      if (i->ws_label != j)
	/* Nop. */
	continue;

      /* Remove the jump instruction `i'. */

      change = WS_TRUE;
      i->ws_label->ws_label_refcount--;

      if (i->next)
	i->next->prev = i->prev;
      else
	compiler->asm_tail = i->prev;

      if (i->prev)
	i->prev->next = i->next;
      else
	compiler->asm_head = i->next;

      /* Continue from the label `j'. */
      i = j;
    }

  return change;
}


static WsBool
opt_dead_code(WsCompilerPtr compiler)
{
  WsBool change = WS_FALSE;
  WsAsmIns *i;

  ws_info(compiler, "optimize: dead code");

  for (i = compiler->asm_head; i; i = i->next)
    {
      WsAsmIns *j;

      if (i->type != WS_ASM_P_JUMP)
	continue;

      /* Skip until the next referenced label is found. */
      for (j = i->next;
	   j && (j->type != WS_ASM_P_LABEL || j->ws_label_refcount == 0);
	   j = j->next)
	{
	  /* Update label reference counts in the deleted block. */
	  if (WS_ASM_P_BRANCH(j))
	    j->ws_label->ws_label_refcount--;
	}

      if (j == i->next)
	/* Nothing to delete. */
	continue;

      /* Delete everything between `i' and `j'. */
      i->next = j;
      if (j)
	j->prev = i;

      change = WS_TRUE;
    }

  return change;
}


static WsBool
opt_peephole(WsCompilerPtr compiler)
{
  WsBool change = WS_FALSE;
  WsAsmIns *i, *i2, *prev;

  ws_info(compiler, "optimize: peephole");

  prev = NULL;
  i = compiler->asm_head;
  while (i)
    {
      /* Two instructions wide peephole. */
      if (i->next)
	{
	  i2 = i->next;

	  /*
	   * {load*,const*}	=>	-
	   * pop
	   */
	  if (i2->type == WS_ASM_POP
	      && (i->type == WS_ASM_P_LOAD_VAR
		  || i->type == WS_ASM_P_LOAD_CONST
		  || i->type == WS_ASM_CONST_0
		  || i->type == WS_ASM_CONST_1
		  || i->type == WS_ASM_CONST_M1
		  || i->type == WS_ASM_CONST_ES
		  || i->type == WS_ASM_CONST_INVALID
		  || i->type == WS_ASM_CONST_TRUE
		  || i->type == WS_ASM_CONST_FALSE))
	      {
		/* Remove both instructions. */
		change = WS_TRUE;

		if (prev)
		  prev->next = i2->next;
		else
		  compiler->asm_head = i2->next;

		i = i2->next;
		continue;
	      }
	}

      /* Move forward. */
      prev = i;
      i = i->next;
    }

  return change;
}

/********************* Global entry point *******************************/

void
ws_asm_optimize(WsCompilerPtr compiler)
{
  WsBool change = WS_TRUE;

  /* While we manage to change the assembler, performe the requested
     optimizations. */
  while (change)
    {
      change = WS_FALSE;

      /* Peephole. */
      if (!compiler->params.no_opt_peephole && opt_peephole(compiler))
	change = WS_TRUE;

      /* Jumps to jump instructions. */
      if (!compiler->params.no_opt_jumps_to_jumps
	  && opt_jumps_to_jumps(compiler))
	change = WS_TRUE;

      /* Jumps to the next instruction. */
      if (!compiler->params.no_opt_jumps_to_next_instruction
	  && opt_jumps_to_next_instruction(compiler))
	change = WS_TRUE;

      /* Unreachable code. */
      if (!compiler->params.no_opt_dead_code && opt_dead_code(compiler))
	change = WS_TRUE;
    }
}
