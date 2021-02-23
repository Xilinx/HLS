===================================================
Extra Clang Tools 7.0.0 (In-Progress) Release Notes
===================================================

.. contents::
   :local:
   :depth: 3

Written by the `LLVM Team <http://llvm.org/>`_

.. warning::

   These are in-progress notes for the upcoming Extra Clang Tools 7 release.
   Release notes for previous releases can be found on
   `the Download Page <http://releases.llvm.org/download.html>`_.

Introduction
============

This document contains the release notes for the Extra Clang Tools, part of the
Clang release 7.0.0. Here we describe the status of the Extra Clang Tools in
some detail, including major improvements from the previous release and new
feature work. All LLVM releases may be downloaded from the `LLVM releases web
site <http://llvm.org/releases/>`_.

For more information about Clang or LLVM, including information about
the latest release, please see the `Clang Web Site <http://clang.llvm.org>`_ or
the `LLVM Web Site <http://llvm.org>`_.

Note that if you are reading this file from a Subversion checkout or the
main Clang web page, this document applies to the *next* release, not
the current one. To see the release notes for a specific release, please
see the `releases page <http://llvm.org/releases/>`_.

What's New in Extra Clang Tools 7.0.0?
======================================

Some of the major new features and improvements to Extra Clang Tools are listed
here. Generic improvements to Extra Clang Tools as a whole or to its underlying
infrastructure are described first, followed by tool-specific sections.

Major New Features
------------------

...

  It aims to provide automated insertion of missing ``#includes`` with a single
  button press in an editor. Integration with Vim and a tool to generate the
  symbol index used by the tool are also part of this release. See the
  `include-fixer documentation`_ for more information.

.. _include-fixer documentation: http://clang.llvm.org/extra/include-fixer.html

Improvements to clang-tidy
--------------------------

- New `xilinx-label-all-loops-check
  <http://clang.llvm.org/extra/clang-tidy/checks/xilinx-label-all-loops-check.html>`_ check

  FIXME: add release notes.

- New `xilinx-stream-in-struct
  <http://clang.llvm.org/extra/clang-tidy/checks/xilinx-stream-in-struct.html>`_ check

  FIXME: add release notes.

- New `xilinx-array-stream-check
  <http://clang.llvm.org/extra/clang-tidy/checks/xilinx-array-stream-check.html>`_ check

  FIXME: add release notes.

- New `xilinx-tb31-process
  <http://clang.llvm.org/extra/clang-tidy/checks/xilinx-tb31-process.html>`_ check

  FIXME: add release notes.

- New `xilinx-tb-xfmat
  <http://clang.llvm.org/extra/clang-tidy/checks/xilinx-tb-xfmat.html>`_ check

  FIXME: add release notes.

- New `xilinx-top-parameters
  <http://clang.llvm.org/extra/clang-tidy/checks/xilinx-top-parameters.html>`_ check

  FIXME: add release notes.

- New `xilinx-xfmat-array-geometry
  <http://clang.llvm.org/extra/clang-tidy/checks/xilinx-xfmat-array-geometry.html>`_ check

  FIXME: add release notes.

- New `xilinx-tb-process
  <http://clang.llvm.org/extra/clang-tidy/checks/xilinx-tb-process.html>`_ check

  FIXME: add release notes.

- New `xilinx-apint-literal-convertation
  <http://clang.llvm.org/extra/clang-tidy/checks/xilinx-apint-literal-convertation.html>`_ check

  FIXME: add release notes.

- New `xilinx-dump-openclkernel
  <http://clang.llvm.org/extra/clang-tidy/checks/xilinx-dump-openclkernel.html>`_ check

  FIXME: add release notes.

- New `xilinx-remove-assert
  <http://clang.llvm.org/extra/clang-tidy/checks/xilinx-remove-assert.html>`_ check

  FIXME: add release notes.

- New `xilinx-resource-pragma-transformer
  <http://clang.llvm.org/extra/clang-tidy/checks/xilinx-resource-pragma-transformer.html>`_ check

  FIXME: add release notes.

- New `xilinx-ssdm-intrinsics-arguments
  <http://clang.llvm.org/extra/clang-tidy/checks/xilinx-ssdm-intrinsics-arguments.html>`_ check

- New `xilinx-systemc-detector
  <http://clang.llvm.org/extra/clang-tidy/checks/xilinx-systemc-detector.html>`_ check

  FIXME: add release notes.

- New `xilinx-warn-mayneed-no-ctor-attribute
  <http://clang.llvm.org/extra/clang-tidy/checks/xilinx-warn-mayneed-no-ctor-attribute.html>`_ check

  FIXME: add release notes.

- New `cppcoreguidelines-avoid-goto
  <http://clang.llvm.org/extra/clang-tidy/checks/cppcoreguidelines-avoid-goto.html>`_ check

  The usage of ``goto`` for control flow is error prone and should be replaced
  with looping constructs. Every backward jump is rejected. Forward jumps are
  only allowed in nested loops.

- New `fuchsia-multiple-inheritance
  <http://clang.llvm.org/extra/clang-tidy/checks/fuchsia-multiple-inheritance.html>`_ check

  Warns if a class inherits from multiple classes that are not pure virtual.

- New `fuchsia-statically-constructed-objects
  <http://clang.llvm.org/extra/clang-tidy/checks/fuchsia-statically-constructed-objects.html>`_ check

  Warns if global, non-trivial objects with static storage are constructed, unless the 
  object is statically initialized with a ``constexpr`` constructor or has no 
  explicit constructor.
  
- New `fuchsia-trailing-return
  <http://clang.llvm.org/extra/clang-tidy/checks/fuchsia-trailing-return.html>`_ check

  Functions that have trailing returns are disallowed, except for those 
  using decltype specifiers and lambda with otherwise unutterable 
  return types.
    
- New alias `hicpp-avoid-goto
  <http://clang.llvm.org/extra/clang-tidy/checks/hicpp-avoid-goto.html>`_ to 
  `cppcoreguidelines-avoid-goto <http://clang.llvm.org/extra/clang-tidy/checks/cppcoreguidelines-avoid-goto.html>`_
  added.

Improvements to include-fixer
-----------------------------

The improvements are...

Improvements to modularize
--------------------------

The improvements are...
