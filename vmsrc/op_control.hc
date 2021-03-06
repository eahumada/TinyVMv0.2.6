/**
 * This is included inside a switch statement.
 */

case OP_IF_ICMPEQ:
case OP_IF_ACMPEQ:
  // Arguments: 2
  // Stack: -2
  do_isub();
  // Fall through!
case OP_IFEQ:
case OP_IFNULL:
  // Arguments: 2
  // Stack: -1
  do_goto (*stackTop-- == 0);
  goto LABEL_ENGINELOOP;
case OP_IF_ICMPNE:
case OP_IF_ACMPNE:
  do_isub();
  // Fall through!
case OP_IFNE:
case OP_IFNONNULL:
  do_goto (*stackTop-- != 0);
  goto LABEL_ENGINELOOP;
case OP_IF_ICMPLT:
  do_isub();
  // Fall through!
case OP_IFLT:
  do_goto (word2jint(*stackTop--) < 0);
  goto LABEL_ENGINELOOP;
case OP_IF_ICMPLE:
  do_isub();
  // Fall through!
case OP_IFLE:
  do_goto (word2jint(*stackTop--) <= 0);
  goto LABEL_ENGINELOOP;
case OP_IF_ICMPGE:
  do_isub();
  // Fall through!
case OP_IFGE:
  do_goto (word2jint(*stackTop--) >= 0);
  goto LABEL_ENGINELOOP;
case OP_IF_ICMPGT:
  do_isub();
  // Fall through!
case OP_IFGT:
  do_goto (word2jint(*stackTop--) > 0);
  goto LABEL_ENGINELOOP;
case OP_JSR:
  // Arguments: 2
  // Stack: +1
  *(++stackTop) = ptr2word (pc + 2);
  // Fall through!
case OP_GOTO:
  // Arguments: 2
  // Stack: +0
  do_goto (true);
  // No pc increment!
  goto LABEL_ENGINELOOP;
case OP_RET:
  // Arguments: 1
  // Stack: +0
  pc = word2ptr (localsBase[pc[0]]);
  #if DEBUG_BYTECODE
  printf ("\n  OP_RET: returning to %d\n", (int) pc);
  #endif
  // No pc increment!
  goto LABEL_ENGINELOOP;

// Notes:
// - Not supported: TABLESWITCH, LOOKUPSWITCH, GOTO_W, JSR_W, LCMP
// - No support for DCMP* or FCMP*.
 
/*end*/







