#ifndef CompilerAssert
// An assert that is evaluated during compilation (and uses no code space).
// If the expression is zero, the compiler will warn that the vector
// _CompilerAssert[] has zero elements, otherwise it is silent.
//
// Lint warning 506 is "Constant value Boolean",
// 762 is "Redundantly declared ... previously declared at line ..."
#define CompilerAssert(e)     \
  /*lint -save -e506 -e762 */ \
      extern char _CompilerAssert[(e)?1:-1] \
  /*lint -restore */

#define CompilerAssertInCode(e)     \
  /*lint -save -e506 -e762 */ \
      { extern char _CompilerAssertInCode[(e)?1:-1]; } \
  /*lint -restore */


#endif

