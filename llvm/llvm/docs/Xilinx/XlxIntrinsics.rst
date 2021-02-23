======================
Xilinx FPGA Intrinsics
======================

.. contents::
   :local:
   :depth: 4

Bit Manipulation Intrinsics
---------------------------

Xilinx has intrinsics for a few important bit manipulation
operations. These allow efficient code generation for some algorithms.

'``llvm.fpga.legacy.part.select.*``' Intrinsics
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This is an overloaded intrinsic. You can use '``llvm.fpga.legacy.part.select``'
on any integer bit width. This is exact the same as
LLVM 2.5 '``llvm.part.select``'.

::

      declare i17 @llvm.fpga.legacy.part.select.i17 (i17 %val, i32 %loBit, i32 %hiBit)
      declare i29 @llvm.fpga.legacy.part.select.i29 (i29 %val, i32 %loBit, i32 %hiBit)

Overview:
"""""""""

The '``llvm.fpga.legacy.part.select``' family of intrinsic functions selects a range of bits
from an integer value and returns them in the same bit width as the original
value.


Arguments
"""""""""

The first argument, '``%val``' and the result may be integer types of  any bit
width but they must have the same bit width. The second and third arguments must
be '``i32``' type since they specify only a bit index.


Semantics:
""""""""""

The operation of the '``llvm.fpga.legacy.part.select``' intrinsic has two modes
of operation: forwards and reverse. If '``%loBit``' is greater than '``%hiBits``'
then the intrinsic operates in reverse mode. Otherwise it operates in forward
mode.

In forward mode, this intrinsic is the equivalent of shifting '``%val``' right
by '``%loBit``' bits and then ANDing it with a mask with only
the '``%hiBit - %loBit``' bits set, as follows.

* The '``%val``' is shifted right (LSHR) by the number of bits specified
  by '``%loBits``'. This normalizes the value to the low order bits.
* The '``%loBits``' value is subtracted from the '``%hiBits``' value to
  determine the number of bits to retain.
* A mask of the retained bits is created by shifting a -1 value.
* The mask is ANDed with <tt>%val</tt> to produce the result.

In reverse mode, a similar computation is made except that the bits are returned
in the reverse order. So, for example, if '``X``' has the
value '``i16 0x0ACF (101011001111)``' and we apply '``part.select(i16 X, 8, 3)``'
to it, we get back the value '``i16 0x0026 (000000100110)``'.


'``llvm.fpga.legacy.part.set.*``' Intrinsics
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This is an overloaded intrinsic. You can use '``llvm.fpga.legacy.part.set``' on
any integer bit width. This is exact the same as LLVM 2.5 '``llvm.part.set``'.

::
      declare i17 @llvm.fpga.legacy.part.set.i17.i9 (i17 %val, i9 %repl, i32 %lo, i32 %hi)
      declare i29 @llvm.fpga.legacy.part.set.i29.i9 (i29 %val, i9 %repl, i32 %lo, i32 %hi)


Overview:
"""""""""
The '``llvm.fpga.legacy.part.set``' family of intrinsic functions replaces a
range of bits in an integer value with another integer value. It returns the
integer with the replaced bits.

Arguments
"""""""""

The first argument, '``%val``', and the result may be integer types of any bit
width, but they must have the same bit width. '``%val``' is the value whose bits
will be replaced.  The second argument, '``%repl``' may be an integer of any bit
width. The third and fourth arguments must be '``i32``' type since they specify
only a bit index.

Semantics:
""""""""""

The operation of the '``llvm.fpga.legacy.part.set``' intrinsic has two modes of
operation: forwards and reverse. If '``%lo``' is greater than '``%hi``' then the
intrinsic operates in reverse mode. Otherwise it operates in forward mode.

For both modes, the '``%repl``' value is prepared for use by either truncating
it down to the size of the replacement area or zero extending it up to that
size.

In forward mode, the bits between '``%lo``' and '``%hi``' (inclusive) are
replaced with corresponding bits from '``%repl``'. That is the 0th bit
in '``%repl``' replaces the '``%lo``'th bit in '``%val``' and etc. up to
the '``%hi``'th bit.

In reverse mode, a similar computation is made except that the bits are
reversed. That is, the '``0``'th bit in '``%repl``' replaces the  '``%hi``' bit
in '``%val``' and etc. down to the '``%lo``'th bit.

Examples:

.. code-block:: llvm
  
  llvm.fpga.legacy.part.set(0xFFFF, 0, 4, 7) ; 0xFF0F
  llvm.fpga.legacy.part.set(0xFFFF, 0, 7, 4) ; 0xFF0F
  llvm.fpga.legacy.part.set(0xFFFF, 1, 7, 4) ; 0xFF8F
  llvm.fpga.legacy.part.set(0xFFFF, F, 8, 3) ; 0xFFE7
  llvm.fpga.legacy.part.set(0xFFFF, 0, 3, 8) ; 0xFE07

