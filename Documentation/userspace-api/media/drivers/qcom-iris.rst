.. SPDX-License-Identifier: GPL-2.0-only

Qualcomm iris video codec driver
================================

The iris driver implements a driver-specific control on its decoder and
encoder video devices, in the range of V4L2_CID_USER_QCOM_IRIS_BASE.

``V4L2_CID_IRIS_SECURE_SESSION (boolean)``
    Make the session secure: the codec then decodes or encodes only in
    protected memory, through the secure context banks of the video
    codec. Both queues take DMA-bufs of protected memory (for instance
    from the TEE protected heap) and nothing else: a buffer of ordinary
    memory fails to map. The control can only be changed before the
    first ``VIDIOC_REQBUFS`` of the session, and secure sessions are only
    offered when the platform isolates the secure context banks (a
    hypervisor owns them). The firmware runs at most three secure
    sessions at a time.
