.. SPDX-License-Identifier: GPL-2.0-only

=====================================
Qualcomm DSP Accelerator (QDA) Driver
=====================================

Introduction
============

The QDA driver is a DRM accel driver for Qualcomm's Hexagon DSPs. It provides
an interface for offloading workloads such as AI inference, computer vision,
audio processing and sensor processing to the DSPs on Qualcomm SoCs. It
speaks the FastRPC protocol to the DSP over RPMsg and uses the DRM GEM and
PRIME infrastructure for buffer management.

The driver binds to the device-tree nodes described by
``Documentation/devicetree/bindings/misc/qcom,fastrpc.yaml``.

Relationship to the FastRPC Driver
==================================

Qualcomm SoCs currently offload DSP work through ``drivers/misc/fastrpc.c``,
a misc character device driver that predates the DRM accel subsystem. QDA is
intended to replace it.

To keep existing user space working, the ``qda_fastrpc_shim`` module
(``CONFIG_DRM_ACCEL_QDA_FASTRPC_SHIM``) registers the same
``/dev/fastrpc-<dsp>`` (and, where applicable, ``/dev/fastrpc-<dsp>-secure``)
device nodes and translates the legacy ioctls onto QDA sessions. The shim
holds only the legacy ABI handling; the protocol, buffer and session logic
all live in the QDA core.

QDA and ``CONFIG_QCOM_FASTRPC`` bind the same device-tree nodes and create
the same device nodes, so they are mutually exclusive. Once the shim has
reached parity with the legacy ABI, ``drivers/misc/fastrpc.c`` is to be
removed.

The shim keeps the legacy secure and non-secure node model. A DSP domain is
secure unless its device-tree node has ``qcom,non-secure-domain``. ADSP, MDSP
and SDSP get a single node, ``/dev/fastrpc-<dsp>`` or
``/dev/fastrpc-<dsp>-secure`` according to the domain. CDSP and GDSP, which
support unsigned protection domains, always get both nodes. On a secure
domain, a client that opened the non-secure node may only create an unsigned
protection domain, and only on a DSP that supports them. As in the legacy
driver, access to either node is controlled by the node's file permissions.

Architecture
============

The QDA driver consists of the following blocks:

1.  **Core driver** (``qda_drv``): device registration, per-open session
    state and DRM accel integration.
2.  **Transport** (``qda_rpmsg``): binds to the DSP's RPMsg channel and sends
    and receives FastRPC messages.
3.  **SoC data** (``qda_soc``): per-SoC parameters such as the position of
    the stream ID in a DSP address and the supported DMA address width.
4.  **Context banks** (``qda_cb``): creates a platform device for each
    ``qcom,fastrpc-compute-cb`` child node of the DSP's device-tree node,
    using ``of_platform_device_create()``. Registering the device is what
    gives it its IOMMU domain, so no driver has to bind to it. They are
    created before the accel device is registered and destroyed after it is
    unplugged, so user space can never reach a context bank that does not
    exist.
5.  **Memory manager** (``qda_memory_manager``, ``qda_memory_dma``): tracks
    the context banks and allocates DMA-coherent memory from the one
    assigned to a session.
6.  **GEM** (``qda_gem``, ``qda_prime``): GEM buffer objects, mmap and PRIME
    import.
7.  **FastRPC core** (``qda_fastrpc``): argument marshalling, invocation
    contexts and the DSP process management calls.
8.  **Legacy interface** (``qda_legacy_ops``, ``qda_fastrpc_shim``): the
    compatibility layer described above.

Memory Management
=================

Buffers are GEM objects backed by DMA-coherent memory allocated through the
session's context bank device, so their DMA address is an address in that
context bank's SMMU address space.

A session is one open file: either an ``/dev/accel/accel*`` file or, with
the shim, a ``/dev/fastrpc-*`` one. It is assigned one context bank,
exclusively, on its first buffer allocation or import. If no context bank is
free, the operation fails with ``-EBUSY``. Context banks are not shared
between sessions, so one session cannot reach another's buffers through an
address the DSP accepts.

Buffer arguments to every ioctl are identified by a GEM handle together with
an offset and a length within that buffer. No ioctl takes a user pointer to
buffer contents or a DMA-BUF file descriptor. To use a buffer from another
driver, user space imports its DMA-BUF with ``DRM_IOCTL_PRIME_FD_TO_HANDLE``
first. Imported buffers must be contiguous in the context bank's address
space, because the DSP is given a single base address per buffer; other
imports are rejected.

The DSP manages its own virtual address space. The AP side only maps buffers
into the SMMU context bank through the DMA API, so QDA does not use
``drm_gpuvm``.

User-Space API
==============

The driver exposes the following ioctls, defined in
``include/uapi/drm/qda_accel.h``:

``DRM_IOCTL_QDA_QUERY``
    Query a property of the DSP selected by ``query_type``:
    ``QDA_QUERY_DSP_NAME`` returns the domain name (for example ``cdsp``),
    which is how user space picks the right ``/dev/accel/accel*`` node, and
    ``QDA_QUERY_DSP_CAPABILITIES`` returns the value of the DSP capability
    selected by ``attribute_id`` (cached per-device after the first
    successful query).

``DRM_IOCTL_QDA_GEM_CREATE``
    Allocate a GEM buffer object and return its handle.

``DRM_IOCTL_QDA_GEM_MMAP_OFFSET``
    Return the offset to pass to ``mmap()`` on the accel file descriptor to
    map a GEM buffer object. Imported buffers cannot be mapped this way.

``DRM_IOCTL_QDA_REMOTE_SESSION_CREATE``
    Create a user process on the DSP from an ELF image given as a GEM handle.
    Creating a signed process domain (``attrs`` without
    ``QDA_INIT_ATTR_UNSIGNED_MODULE``) requires ``CAP_SYS_ADMIN``.

``DRM_IOCTL_QDA_REMOTE_MMAP`` / ``DRM_IOCTL_QDA_REMOTE_MUNMAP``
    Create or remove a persistent mapping of a buffer in the DSP's address
    space. ``request`` selects between mapping a whole buffer, optionally at
    a hinted address (``QDA_REQUEST_MMAP``, ``QDA_REQUEST_MUNMAP``), and
    mapping a region with SMMU attributes (``QDA_REQUEST_MEM_MAP``,
    ``QDA_REQUEST_MEM_UNMAP``). An unmap must use the same request the
    mapping was made with.

    A mapping does not keep the buffer alive. Its GEM handle has to stay
    open for as long as the DSP uses the mapping, and must only be closed
    after the mapping has been removed.

``DRM_IOCTL_QDA_REMOTE_INVOKE``
    Call a method on the DSP. This is the main execution primitive.

    The scalars value ``sc`` selects the method and describes the arguments:
    bits 28..24 are the method index, bits 23..16 the number of input
    buffers, bits 15..8 the number of output buffers, bits 7..4 the number
    of input handles and bits 3..0 the number of output handles. ``args``
    points to an array with one entry per argument, in that order, each
    naming a buffer by GEM handle together with an offset and a length
    within it.

    The handles must stay open for the duration of the call. Buffers are
    given to the DSP by their SMMU addresses, so they do not need a prior
    ``REMOTE_MMAP``; use ``REMOTE_MMAP`` for buffers the DSP keeps using
    across calls.

``DRM_IOCTL_QDA_REMOTE_SESSION_ATTACH``
    Attach the session to an existing protection domain instead of creating
    a user process: ``QDA_ATTACH_ROOT_PD``, ``QDA_ATTACH_AUDIO_PD`` or
    ``QDA_ATTACH_SENSORS_PD``. Requires ``CAP_SYS_ADMIN``, and fails with
    ``-EBUSY`` if a process has already been created on the session. The
    first Audio PD attach on a device additionally hands the DSP the remote
    heap reserved for it in the device tree; later attaches reuse it.

``DRM_IOCTL_QDA_SET_OPTION``
    Set a session option. ``QDA_SET_OPTION_POLL_MODE`` switches a dynamic
    invocation in a user PD between waiting for an interrupt-based reply and
    polling shared memory for completion, trading CPU cycles for lower
    round-trip latency. Rejected with ``-EOPNOTSUPP`` on DSPs that do not
    support polling.

Usage Example
=============

A typical lifecycle for a user-space application:

1.  **Discovery**: open ``/dev/accel/accel*`` and use
    ``DRM_IOCTL_QDA_QUERY`` to find the node for the wanted DSP domain.
2.  **Initialization**: call ``DRM_IOCTL_QDA_REMOTE_SESSION_CREATE`` to
    create a process on the DSP.
3.  **Memory**: allocate buffers with ``DRM_IOCTL_QDA_GEM_CREATE``, or import
    DMA-BUFs from other drivers with ``DRM_IOCTL_PRIME_FD_TO_HANDLE``.
4.  **Execution**: use ``DRM_IOCTL_QDA_REMOTE_INVOKE`` to run functions on
    the DSP.
5.  **Cleanup**: close the file descriptor. The DSP process is released and
    the session's buffers and context bank are freed.

DSP Restart
===========

When the DSP's RPMsg channel goes away, for example because the remote
processor is stopped or crashes, invocations still waiting for a reply fail
with ``-EPIPE`` and later ioctls fail with ``-ENODEV``. The DSP comes back
as a new accel device, possibly with a different minor number, so user space
has to close the file descriptor and repeat discovery.

Debugging
=========

The driver's debug messages use the DRM driver debug category. Enable them
with:

.. code-block:: bash

    echo 0x2 > /sys/module/drm/parameters/debug

or, where ``drm`` is built in, with ``drm.debug=0x2`` on the kernel command
line.

Testing
=======

The driver is exercised with the ``fastrpc_test`` utility from the
open-source FastRPC user-space library, built with the open-source QAIC IDL
compiler:

* FastRPC library: https://github.com/qualcomm/fastrpc
* QAIC compiler: https://github.com/qualcomm/QAIC

For example, to run the tests on the CDSP with an unsigned process domain:

.. code-block:: bash

    fastrpc_test -d 3 -U 1 -t linux -a v68
