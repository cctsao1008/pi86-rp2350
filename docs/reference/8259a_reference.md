# 8259A Reference Notes

## Purpose

This document records external references used for architecture comparison and future feature planning.

Reference implementation:

https://github.com/zainmo11/8259A-PROGRAMMABLE-INTERRUPT-CONTROLLER

## Usage

The reference is used for:

- architecture comparison
- feature checklist
- future OCW2/OCW3 planning
- priority and cascade behavior study

It is not used as a source dependency.

## Current pi86_pic Scope

Implemented and validated:

- programmable initialization
- interrupt masking
- request tracking
- in-service tracking
- fixed priority IRQ handling
- interrupt acknowledge sequence
- non-specific EOI

Future candidates:

- OCW2 advanced modes
- OCW3 register readback
- multiple IRQ priority validation
- cascade support
