# Fuchsia product definitions

This directory contains definitions for a variety of product configurations. The
products are organized into tracks that build on one another to product a fully
featured system.

## Core track

### Bringup

A minimal system used to exercise kernel and core drivers and to bring up new
boards.

### Base

Self-updating system with core system services, connectivity, and metrics
reporting.

Builds on [bringup](#bringup)

## Workstation track

### Terminal

A system with a simple graphical user interface with a command-line terminal.

Builds on [base](#base).

### Workstation

A system that software developers can use to get work done.

Builds on [terminal](#terminal).

## Speaker

A modular, emphermal system for devices that have audio input and output. Lacks
a graphical user interface.

Builds on [base](#base).

## Router

A system for routing network traffic over wireless networks.

Builds on [base](#base).