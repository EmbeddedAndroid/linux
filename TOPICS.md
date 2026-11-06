# TOPICS: Linux qcom/integration

Base: linux-next tag next-20261103 (d6bcfc94eb4da). Every qcom/topic/<name> branch is one subject area shared by several boards; every qcom/<platform> branch carries only that SoC's DT, bindings and drivers. Topics and platforms are built on the base (three topics stack, see "on"); qcom/integration merges every topic, then every platform, in the order below, and records each conflict resolution in its merge commit message.

Tag: qcom/integration-20261105 (this state; earlier: qcom/integration-20261104b (round 2), qcom/integration-20261104 (round 1)). Next refresh: rebase every branch on the newest next-YYYYMMDD, drop what landed, merge again, tag qcom/integration-YYYYMMDD.

Status legend: FROMLIST = posted upstream by its author, link to the revision carried; "posted by" = an exact-subject [PATCH] found on lore with lei; "not posted" = no exact-subject [PATCH] on lore (lei, all lists, 2026-11-04).

## Branches

| branch | on | commits | ours | posted (FROMLIST or on lore) | not posted | boards |
|---|---|---|---|---|---|---|
| qcom/topic/rproc-pas-tee | next-20261103 | 2 | 1 | 1 | 1 | every OP-TEE PAS board (all ports below) |
| qcom/topic/pm-clk | next-20261103 | 2 | 2 | 0 | 2 | every board running alsa-dsp-restart: lanai, kailua, eliza, pakala, hamoa, purwa, kaanapali, glymur, mahua, talos, monaco, kodiak, agatti |
| qcom/topic/fw-devlink | next-20261103 | 6 | 6 | 0 | 6 | alsa-dsp-restart boards: lanai, kailua, eliza, pakala, hamoa, purwa, glymur, mahua, talos, monaco, kodiak, agatti |
| qcom/topic/qdsp6-ssr | next-20261103 | 8 | 8 | 0 | 8 | audio boards: lanai, kailua, eliza, pakala, hamoa, purwa, glymur, mahua, talos, monaco, kodiak, agatti |
| qcom/topic/soundwire-ssr | next-20261103 | 5 | 4 | 1 | 4 | SoundWire boards: lanai, kailua, eliza, pakala, hamoa, purwa, kaanapali, glymur, mahua, kodiak, agatti |
| qcom/topic/codec-ssr | next-20261103 | 7 | 7 | 0 | 7 | lanai, kailua, eliza, pakala (WCD939x/WSA884x), kailua (WCD938x), glymur, mahua, kodiak, agatti (wcd-common, WSA883x) |
| qcom/topic/adsp-restart-audio | next-20261103 | 4 | 3 | 1 | 3 | hamoa, purwa, glymur, mahua (all three); lanai, kailua, eliza, pakala, kaanapali, kodiak (DAI link names: SoundWire DAIs in the card) |
| qcom/topic/pas-video | next-20261103 | 4 | 0 | 4 | 0 | every iris/venus board on OP-TEE PAS |
| qcom/topic/iommu-ranges | next-20261103 | 2 | 0 | 2 | 0 | kaanapali, eliza (iris DTs with context bank subnodes) |
| qcom/topic/iris-el2 | qcom/topic/pas-video | 8 | 1 | 7 | 1 | lanai, kailua, pakala, hamoa, purwa, kaanapali, monaco, kodiak, agatti |
| qcom/topic/iris-kaanapali | qcom/topic/iris-el2 | 4 | 0 | 4 | 0 | kaanapali |
| qcom/topic/adreno-zap-el2 | next-20261103 | 3 | 3 | 0 | 3 | lanai, kailua, agatti |
| qcom/topic/adreno-gmu | next-20261103 | 2 | 2 | 0 | 2 | eliza |
| qcom/topic/ufs-qcom | next-20261103 | 3 | 2 | 1 | 2 | eliza, kaanapali |
| qcom/topic/arm-smmu-qcom | next-20261103 | 1 | 1 | 0 | 1 | glymur, mahua, hamoa, purwa |
| qcom/topic/oryon-errata | next-20261103 | 1 | 1 | 0 | 1 | glymur, mahua, kaanapali |
| qcom/topic/msm-dp | next-20261103 | 1 | 1 | 0 | 1 | glymur, mahua, hamoa, purwa |
| qcom/topic/cpuidle-psci | next-20261103 | 1 | 1 | 0 | 1 | talos, monaco |
| qcom/topic/rtc-pm8xxx | next-20261103 | 1 | 1 | 0 | 1 | kodiak, lemans, monaco |
| qcom/topic/fastrpc | next-20261103 | 6 | 0 | 6 | 0 | upstream-review queue (kodiak, monaco testing) |
| qcom/topic/rpmhpd-nsp | next-20261103 | 1 | 1 | 0 | 1 | lemans, monaco |
| qcom/topic/qmp-combo-pd | next-20261103 | 2 | 2 | 0 | 2 | glymur, mahua |
| qcom/topic/apss-wdt | next-20261103 | 3 | 3 | 0 | 3 | kailua, lanai, eliza |
| qcom/lanai | next-20261103 | 8 | 8 | 0 | 8 | sm8650-qrd |
| qcom/kailua | next-20261103 | 7 | 7 | 0 | 7 | sm8550-hdk, sm8550-qrd |
| qcom/eliza | next-20261103 | 21 | 15 | 6 | 15 | eliza-mtp, eliza-evk |
| qcom/pakala | next-20261103 | 4 | 4 | 0 | 4 | sm8750-mtp |
| qcom/hamoa | next-20261103 | 2 | 2 | 0 | 2 | hamoa-iot-evk (x1-el2) |
| qcom/purwa | next-20261103 | 2 | 2 | 0 | 2 | purwa-iot-evk |
| qcom/kaanapali | next-20261103 | 5 | 3 | 2 | 3 | kaanapali-mtp |
| qcom/glymur | next-20261103 | 9 | 9 | 0 | 9 | glymur-crd, mahua-crd |
| qcom/talos | next-20261103 | 2 | 2 | 0 | 2 | qcs615-ride |
| qcom/monaco | next-20261103 | 9 | 8 | 1 | 6 | monaco-arduino-monza |
| qcom/lemans | next-20261103 | 3 | 3 | 0 | 3 | lemans-evk |
| qcom/kodiak | next-20261103 | 7 | 4 | 2 | 5 | qcs6490-rb3gen2 |
| qcom/agatti | next-20261103 | 3 | 3 | 0 | 3 | qrb2210-arduino-imola |
| qcom/nord | next-20261103 | 4 | 3 | 0 | 4 | iq10-evk |
| qcom/eliza-camera | qcom/eliza | 39 | 19 | 18 | 20 | eliza-mtp camera overlays |

qcom/integration 0bcc0b835142: 163 commits, 37 merges (plus this file).

## Conflicts resolved in the merges

- Merge branch 'qcom/eliza' into qcom/integration: Conflicts in drivers/media/platform/qcom/iris/iris_platform_common.h and iris_probe.c: qcom/topic/iris-kaanapali adds the Kaanapali platform data and qcom/eliza the Eliza platform data at the same place; keep both, Eliza first in name order.

Conflicts resolved while building the branches (cherry-picks onto the base) are in the branch sections.

## Not carried from the port kernels

Every carried commit of every port kernel is accounted for in inv/mapping.tsv of the layout workspace (destination branch or reason). Not carried here: commits already in the base (other revisions included), the GPU SMMU aperture series (obsolete: BL31 boot-time write), older revisions and local re-implementations of posted patches, diagnostics, EUD (never published), the Agatti venus EL2 commits (Agatti video is iris upstream now; the agatti-el2 video-firmware node replaces them), and the Nord commits that are upstream, belong to the dropped SCMI-over-OP-TEE power stack, or wait for Nord USB/PMIC/display/GPU/video/PCIe/DSP series to land upstream.

qcom/eliza-camera (on qcom/eliza) holds the Eliza camera and is NOT merged here.

## qcom/topic/rproc-pas-tee

Remote processor stop/start with the PAS TEE (OP-TEE) backend: no stale resource table size when the firmware has none; release proxy resources once when a start fails. Boards: every OP-TEE PAS board (all ports below). On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| bd3b1b5bf487c | FROMLIST: remoteproc: qcom: pas: pass no resource table when the firmware has none | Jorge Ramirez-Ortiz | FROMLIST: https://lore.kernel.org/r/20261005125411.2151326-1-jorge.ramirez@oss.qualcomm.com |
| 09f51c3f16a36 | remoteproc: qcom: pas: release proxy resources once on start failure | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/topic/pm-clk

pm_clk_resume() returns clock enable errors instead of resuming a device with dead clocks (LPASS macros and LPI during an ADSP restart), with KUnit tests. Boards: every board running alsa-dsp-restart: lanai, kailua, eliza, pakala, hamoa, purwa, kaanapali, glymur, mahua, talos, monaco, kodiak, agatti. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 9317d0ef1589c | PM: clk: return clock enable errors from pm_clk_resume() | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| fdb4a534231b7 | PM: clk: add KUnit tests for pm_clk_resume() | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/topic/fw-devlink

fw_devlink for suppliers created after boot (glink/smd edge children such as q6prm clocks) and for suppliers that come back after an ADSP restart; genpd provider removal waits for devlink removal; KUnit tests. Boards: alsa-dsp-restart boards: lanai, kailua, eliza, pakala, hamoa, purwa, glymur, mahua, talos, monaco, kodiak, agatti. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 025a3641dd1b8 | driver core: fw_devlink: add a flag for fwnodes populated later | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| e14cc78cf5d14 | driver core: fw_devlink: test a child fwnode populated later | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| cd66b044248f4 | remoteproc: qcom: mark glink and smd edges as populated later | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 6b1a6e70f6865 | driver core: fw_devlink: restore fwnode links of deleted suppliers | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 15fd2923c44bc | driver core: fw_devlink: test a supplier device added again | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 6ff65369e1ebc | pmdomain: core: wait for devlink removal in of_genpd_del_provider() | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/topic/qdsp6-ssr

QDSP6 audio clients survive an ADSP restart (q6prm, q6dsp-lpass-clocks, q6apm, q6afe, q6core, apr GPR, card DAI names, sc8280xp headset jack). Boards: audio boards: lanai, kailua, eliza, pakala, hamoa, purwa, glymur, mahua, talos, monaco, kodiak, agatti. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 4d26e63f6fa2e | ASoC: qcom: q6prm: fail requests once the service is removed | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 75d9fc3afaeea | ASoC: qcom: q6dsp-lpass-clocks: set drvdata before registering clocks | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| fc8d1fe11fb36 | ASoC: qcom: common: keep a copy of the DAI link names | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 420bad43841a9 | soc: qcom: apr: set the GPR service callback before its probe | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 7feef094b44f3 | ASoC: qcom: q6apm: ask for the DSP state again until it answers | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| f17ad8f019a78 | ASoC: qcom: q6core: treat a rejected state query as DSP not ready | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| e43b9d36f70f6 | ASoC: qcom: q6afe: wait for the DSP before registering its clients | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| ae3aecc14e9a7 | ASoC: qcom: sc8280xp: release the WCD headset jack when a link exits | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/topic/soundwire-ssr

Qualcomm SoundWire controller rebind and re-enumeration across an ADSP restart and clock stop. Boards: SoundWire boards: lanai, kailua, eliza, pakala, hamoa, purwa, kaanapali, glymur, mahua, kodiak, agatti. On: next-20261103.

Adaptation: qcom/topic/soundwire-ssr, "FROMLIST: soundwire: qcom: quiesce runtime PM before removing bus" (d87f85c263905): swrm_runtime_resume() gained enable_irq(ctrl->irq) after the hclk enable upstream (6c067677192f0, "soundwire: qcom: disable the interrupt before gating the clock"). Kept the patch's clk_prepare_enable() error path (re-enable the wake IRQ and return) and moved enable_irq(ctrl->irq) after it, so the controller IRQ is only re-enabled once the clock is on.

| commit | subject | author | upstream status |
|---|---|---|---|
| a97b983159c9a | FROMLIST: soundwire: qcom: quiesce runtime PM before removing bus | Myeonghun Pak | FROMLIST: https://lore.kernel.org/r/20260913021356.60079-1-mhun512@gmail.com |
| 50fdc115ef99f | soundwire: qcom: use the interrupt trigger type from the firmware | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 508ceafbeda74 | soundwire: qcom: ignore the peripheral status while leaving clock stop | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| f61fc4b57ab4c | soundwire: qcom: enumerate peripherals that attach again as device 0 | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 81048d3c10800 | soundwire: qcom: enumerate a peripheral that kept its device number | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/topic/codec-ssr

WCD938x/WCD939x/WSA883x codecs unbind and rebind cleanly across an ADSP restart. Boards: lanai, kailua, eliza, pakala (WCD939x/WSA884x), kailua (WCD938x), glymur, mahua, kodiak, agatti (wcd-common, WSA883x). On: next-20261103.

Adaptation: qcom/topic/codec-ssr, "ASoC: codecs: wsa883x: disable the vdd supply when the device goes away" (6fbd581047fe8): no textual conflict, but the build failed (label 'err' used but not defined): ea63182d71956 ("ASoC: codecs: wsa883x: disable runtime PM on unbind") turned pm_runtime_enable() into devm_pm_runtime_enable() with a goto to the err label this patch drops. Adapted (adapt/6fbd581047fe.sh): that error now returns directly, the devm action disables the supply.

| commit | subject | author | upstream status |
|---|---|---|---|
| d26ec2d52b39a | ASoC: codecs: wcd-common: ignore slave interrupts before the codec binds | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 7258cf770c189 | ASoC: codecs: wcd939x: remove the interrupt chip when unbinding | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 95e306e327274 | ASoC: codecs: wcd939x-sdw: mask codec interrupts until the codec binds | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| e799540cdf407 | ASoC: codecs: wcd938x: remove the interrupt chip when unbinding | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 2f723d11295f8 | ASoC: codecs: wcd938x-sdw: mask codec interrupts until the codec binds | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| dc05818118cfd | ASoC: codecs: wsa883x: disable the vdd supply when the device goes away | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 71fb78c33cd8b | ASoC: codecs: wsa883x: initialise the amplifier when it attaches again | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/topic/adsp-restart-audio

Generic fixes on the ADSP restart path: the reset-gpio device survives its GPIO controller rebinding (WSA884x resets on the LPASS LPI pins), ASoC DAI link names owned by the card, the x1e80100 headset jack recreated with the card. Boards: hamoa, purwa, glymur, mahua (all three); lanai, kailua, eliza, pakala, kaanapali, kodiak (DAI link names: SoundWire DAIs in the card). On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 44f9aa3fe80dd | FROMLIST: reset: gpio: Parent the reset-gpio device to the GPIO controller | Abel Vesa | FROMLIST: https://lore.kernel.org/r/20261026-b4-reset-gpio-parent-to-gpio-controller-v1-1-11549e6ea930@oss.qualcomm.com |
| e9397d22b66c2 | gpio: shared: match the reset-gpio device without its parent | Tyler Baker | not posted (lei, all lists, 2026-11-06) |
| dd822e1e5a745 | ASoC: soc-core: keep the DAI link names owned by the card | Tyler Baker | not posted (lei, all lists, 2026-11-06) |
| 0d6dc102ae12e | ASoC: qcom: x1e80100: recreate the headset jack with the card | Tyler Baker | not posted (lei, all lists, 2026-11-06) |

## qcom/topic/pas-video

Sumit Garg's PAS video ops: video memory protection and set_remote_state through the PAS service, so iris/venus work with the TEE backend. Boards: every iris/venus board on OP-TEE PAS. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 36a8365faab73 | FROMLIST: media: qcom: Pass proper PAS ID to set_remote_state API | Sumit Garg | FROMLIST: https://lore.kernel.org/r/20261027-pas-id-media-v10-1-8a5312d3050e@oss.qualcomm.com |
| 85d9faa9d51b0 | FROMLIST: firmware: qcom: pas: Add optional video memory protection op | Sumit Garg | FROMLIST: https://lore.kernel.org/r/20261027-pas-video-v2-1-5b52bca5add6@oss.qualcomm.com |
| 2591d1d0e0d45 | FROMLIST: media: qcom: Use PAS API for video memory protection | Sumit Garg | FROMLIST: https://lore.kernel.org/r/20261027-pas-video-v2-2-5b52bca5add6@oss.qualcomm.com |
| ecf2f01fa1b25 | FROMLIST: firmware: qcom: scm: Drop export of qcom_scm_mem_protect_video_var() | Sumit Garg | FROMLIST: https://lore.kernel.org/r/20261027-pas-video-v2-3-5b52bca5add6@oss.qualcomm.com |

## qcom/topic/iommu-ranges

of_iommu support for the "iommu-ranges" property the iris context bank subnodes use (posted separately from the context bank series since its v6); its dt-schema half is dt-schema PR 207. Boards: kaanapali, eliza (iris DTs with context bank subnodes). On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 872cd7ddc18a6 | FROMLIST: iommu: use resource_size_t for reserved region length | Vikash Garodia | FROMLIST: https://lore.kernel.org/r/20261103-add_iommu_ranges-v6-1-361f3921a133@oss.qualcomm.com |
| b205768874124 | FROMLIST: of: Add support for "iommu-ranges" on a device node | Vikash Garodia | FROMLIST: https://lore.kernel.org/r/20261103-add_iommu_ranges-v6-2-361f3921a133@oss.qualcomm.com |

## qcom/topic/iris-el2

One iris EL2 implementation for every board: Vikash Garodia's context bank bindings and devices (v6, incl. the video-firmware node) and the iris secure PAS boot with a Linux-managed IOMMU (v11), which creates the video-firmware device and maps the firmware for its stream. Boards: lanai, kailua, pakala, hamoa, purwa, kaanapali, monaco, kodiak, agatti. On: qcom/topic/pas-video.

Adaptation: qcom/topic/iris-el2 now sits on qcom/topic/iris-cb (Vikash Garodia, vpu_iommu_iova_handling v5 + kaanapali-iris v4), which sits on qcom/topic/pas-video. The v1 binding of the same series that Lanai carried ("FROMLIST: dt-bindings: media: qcom,sm8550-iris: Add vpu sub nodes", d22943f007e3e, 2026-07-09) is dropped: v5 moves the video-firmware child into qcom,venus-common.yaml. - "FROMLIST: media: iris: Enable Secure PAS support with IOMMU managed by Linux": struct iris_core keeps v5's np_dev/p_dev and adds fw_dev and pas_ctx (both documented). - "media: iris: create the video-firmware device": v5 creates only the non-pixel and pixel context bank devices, so the video-firmware device is still created here. Probe order: context bank devices, IOVA reservation, video-firmware device, v4l2; errors unwind in reverse (new label err_deinit_fw_dev before err_unresv_iova_region). Remove unregisters the video-firmware device before the IOVA region and the context bank devices. The commit message still says "this tree has no context bank devices": true for the firmware stream only, reword before posting.

| commit | subject | author | upstream status |
|---|---|---|---|
| c606fd538ed81 | FROMLIST: dt-bindings: media: qcom,venus: Add context bank subnodes to common schema | Vikash Garodia | FROMLIST: https://lore.kernel.org/r/20261103-vpu_iommu_iova_handling-v6-1-e4b6ad864f8f@oss.qualcomm.com |
| 9747a0b8edf6f | FROMLIST: dt-bindings: media: qcom,sm8550-iris: Add context bank subnodes | Vikash Garodia | FROMLIST: https://lore.kernel.org/r/20261103-vpu_iommu_iova_handling-v6-2-e4b6ad864f8f@oss.qualcomm.com |
| ffa04a5398b04 | FROMLIST: dt-bindings: media: qcom,sm8750-iris: Add context bank subnodes | Vikash Garodia | FROMLIST: https://lore.kernel.org/r/20261103-vpu_iommu_iova_handling-v6-3-e4b6ad864f8f@oss.qualcomm.com |
| abe58c51f9069 | FROMLIST: media: iris: Add non-pixel and pixel context bank devices | Vikash Garodia | FROMLIST: https://lore.kernel.org/r/20261103-vpu_iommu_iova_handling-v6-4-e4b6ad864f8f@oss.qualcomm.com |
| ad13304148e40 | FROMLIST: media: iris: Route buffers to the matching context bank device | Vikash Garodia | FROMLIST: https://lore.kernel.org/r/20261103-vpu_iommu_iova_handling-v6-5-e4b6ad864f8f@oss.qualcomm.com |
| 30a0241d08324 | FROMLIST: media: iris: Skip DMA mask setup when the core device has no IOMMU | Vikash Garodia | FROMLIST: https://lore.kernel.org/r/20261103-vpu_iommu_iova_handling-v6-6-e4b6ad864f8f@oss.qualcomm.com |
| cb7040c60ec97 | FROMLIST: media: iris: Enable Secure PAS support with IOMMU managed by Linux | Mukesh Ojha | FROMLIST: https://lore.kernel.org/r/20260917-glymur-v11-2-e0c8bc914742@oss.qualcomm.com |
| 17cd047757a83 | media: iris: release the PAS metadata after the firmware boot | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/topic/iris-kaanapali

Kaanapali iris (Vikash Garodia's kaanapali-iris v4): binding, platform data, iris4 H265 line buffers, VPU NoC power-off wait. Boards: kaanapali. On: qcom/topic/iris-el2.

| commit | subject | author | upstream status |
|---|---|---|---|
| c1edac3449370 | FROMLIST: media: iris: wait for vpu NoC to enter low power during power off | Vikash Garodia | FROMLIST: https://lore.kernel.org/r/20261001-kaanapali-iris-v4-1-642f9ac5e699@oss.qualcomm.com |
| 3d9f23e06d55e | FROMLIST: media: dt-bindings: qcom-kaanapali-iris: Add kaanapali video codec binding | Vikash Garodia | FROMLIST: https://lore.kernel.org/r/20261001-kaanapali-iris-v4-2-642f9ac5e699@oss.qualcomm.com |
| f6fa08c9674ba | FROMLIST: media: iris: add iris4 specific H265 line buffer calculation | Vikash Garodia | FROMLIST: https://lore.kernel.org/r/20261001-kaanapali-iris-v4-3-642f9ac5e699@oss.qualcomm.com |
| e61ab50951195 | FROMLIST: media: iris: add platform data for kaanapali | Vikash Garodia | FROMLIST: https://patch.msgid.link/20261001-kaanapali-iris-v4-4-642f9ac5e699@oss.qualcomm.com |

## qcom/topic/adreno-zap-el2

Adreno zap shader at EL2: map the zap region for the CP stream named in the zap-shader node; A702 zap resume. Boards: lanai, kailua, agatti. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 323f0b76986c8 | dt-bindings: display/msm: gpu: describe the zap shader stream | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 2d3a0b824476b | drm/msm/adreno: map the zap shader for its stream | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| c176f629165db | drm/msm/a6xx: resume the zap shader on the A702 | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/topic/adreno-gmu

a6xx GMU: tolerate an empty GMxC rail in cmd-db, fail probe when RPMh votes cannot be built. Boards: eliza. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 81c89a673eb7a | drm/msm/a6xx: ignore an empty GMxC rail in the command DB | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| a89e97589a943 | drm/msm/a6xx: fail the GMU probe when the RPMh votes cannot be built | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/topic/ufs-qcom

UFS: reprogram MCQ/ESI after a host reset; defer probe until the device supplies are on. Boards: eliza, kaanapali. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 77f1ca5a8f67e | FROMLIST: scsi: ufs: qcom: Reprogram ESI after a host controller reset | Joonhoe Kim | FROMLIST: https://lore.kernel.org/r/20261022165006.1275-1-26rote@gmail.com |
| 9a4719c1a6d7f | scsi: ufs: core: Reconfigure MCQ before the device init after a reset | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 6f5128cf9fb94 | scsi: ufs: qcom: defer probe before the device supplies are on | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/topic/arm-smmu-qcom

Keep the firmware stream mappings on the Adreno SMMU-500 at EL2. Boards: glymur, mahua, hamoa, purwa. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 19254868f5ac5 | iommu/arm-smmu-qcom: keep firmware stream mappings on the Adreno SMMU-500 | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/topic/oryon-errata

Apply the Oryon CNTVOFF_EL2 erratum workaround to Oryon X2. Boards: glymur, mahua, kaanapali. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| f99d371c0f808 | arm64: errata: apply the Oryon CNTVOFF_EL2 workaround to X2 | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/topic/msm-dp

drm/msm/dp: skip link teardown for a link never enabled. Boards: glymur, mahua, hamoa, purwa. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 72ba9dff598c8 | drm/msm/dp: skip link teardown when the link was never enabled | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/topic/cpuidle-psci

cpuidle-psci: fall back to PC mode when the firmware denies OSI mode. Boards: talos, monaco. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 077c54e55e451 | cpuidle: psci: fall back to PC mode when OSI mode is denied | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/topic/rtc-pm8xxx

rtc-pm8xxx: a missing UEFI RTC offset reads as zero. Boards: kodiak, lemans, monaco. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| e694e56a9a92b | rtc: pm8xxx: treat a missing UEFI offset as zero | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/topic/fastrpc

fastrpc: register the device nodes only after the channel is set up. Boards: upstream-review queue (kodiak, monaco testing). On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 1764011cbf2a6 | FROMLIST: misc: fastrpc: iterate CB nodes manually instead of of_platform_populate | Vinayak Katoch | FROMLIST: https://lore.kernel.org/r/20261102-dup-sessions-v7-1-03f06c014e78@oss.qualcomm.com |
| aa75424e2c65f | FROMLIST: misc: fastrpc: move ADSP duplicate session creation to the driver | Vinayak Katoch | FROMLIST: https://lore.kernel.org/r/20261102-dup-sessions-v7-2-03f06c014e78@oss.qualcomm.com |
| 89d2735a9ba5d | FROMLIST: dt-bindings: misc: qcom,fastrpc: deprecate qcom,nsessions | Vinayak Katoch | FROMLIST: https://lore.kernel.org/r/20261102-dup-sessions-v7-3-03f06c014e78@oss.qualcomm.com |
| 238a2573e44d9 | FROMLIST: misc: fastrpc: fix NULL rpdev dereference when session alloc fails | Vinayak Katoch | FROMLIST: https://lore.kernel.org/r/20261022-fastrpc-probe-fixes-v2-1-a6b3f6f62571@oss.qualcomm.com |
| 76309f1841b4e | FROMLIST: misc: fastrpc: fix init ordering race in rpmsg probe | Vinayak Katoch | FROMLIST: https://lore.kernel.org/r/20261022-fastrpc-probe-fixes-v2-2-a6b3f6f62571@oss.qualcomm.com |
| 6eddf03f6f168 | FROMLIST: misc: fastrpc: map ADSP remote heap into remoteproc IOMMU domain | Anandu Krishnan E | FROMLIST: https://lore.kernel.org/r/20261027-fastrpc-kvm-iommu-v3-v3-1-f3cd5c030c78@oss.qualcomm.com |

## qcom/topic/rpmhpd-nsp

rpmhpd: do not clamp the NSP rails to their highest corner before sync_state. Boards: lemans, monaco. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 753fdc26b9127 | pmdomain: qcom: rpmhpd: don't clamp the NSP rails until sync_state | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/topic/qmp-combo-pd

QMP combo PHY with more than one power domain (binding and driver). Boards: glymur, mahua. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 819eb6c623c93 | dt-bindings: phy: qcom,sc8280xp-qmp-usb43dp: add the Glymur USB30 GDSC | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 9cc988e3cddde | phy: qcom: qmp-combo: attach to multiple power domains | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/topic/apss-wdt

APSS watchdog compatibles for SM8550, SM8650 and Eliza in one binding file (folded from the three platform branches). Boards: kailua, lanai, eliza. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 7621bb796b2c1 | dt-bindings: watchdog: qcom-wdt: document the SM8550 APSS watchdog | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 33ea25049e46c | dt-bindings: watchdog: qcom-wdt: document the SM8650 APSS watchdog | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 90fe1ed809e15 | dt-bindings: watchdog: qcom-wdt: add Eliza | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/lanai

SM8650 QRD: EL2 overlay, APSS watchdog, DSPs and GPU zap through the PAS service, iris at EL2. Boards: sm8650-qrd. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 0f1aadb534851 | arm64: dts: qcom: sm8650: add the EL2 overlay | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 4098f538d6f9d | arm64: dts: qcom: sm8650: add the APSS watchdog | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 1df02a3a52592 | arm64: dts: qcom: sm8650-el2: enable the APSS watchdog | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 195f5c0f7dc0e | dt-bindings: remoteproc: qcom,pas-common: allow a second stream | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 7ac0db9e7e717 | arm64: dts: qcom: sm8650-el2: start the DSPs through the PAS service | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| ea12b6250f4f8 | arm64: dts: qcom: sm8650-el2: load the GPU zap shader | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 606ecd9673766 | arm64: dts: qcom: sm8650-el2: describe the GPU zap shader stream | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 1746f7a9b74c9 | arm64: dts: qcom: sm8650-el2: enable iris | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/kailua

SM8550 HDK/QRD: EL2 overlay, APSS watchdog, DSP PAS, GPU zap with its stream, iris at EL2. Boards: sm8550-hdk, sm8550-qrd. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| d258bef164b09 | arm64: dts: qcom: sm8550: add the EL2 overlay | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 3964951a1e7ed | arm64: dts: qcom: sm8550: add the APSS watchdog | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| b9f50e1764ab6 | arm64: dts: qcom: sm8550-el2: enable the APSS watchdog | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| e1cb5b0e33bed | arm64: dts: qcom: sm8550-el2: start the DSPs through the PAS service | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 81a2783403441 | arm64: dts: qcom: sm8550-el2: load the GPU zap shader | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 19f89c1242f47 | arm64: dts: qcom: sm8550-el2: enable iris | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 654fd55936ef6 | arm64: dts: qcom: sm8550-el2: describe the GPU zap shader stream | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/eliza

SM7750 (Eliza) MTP/EVK: EL2 overlay, watchdog, PMUs, QUP, FastRPC, iris, audio, camcc NoC path, vreg_l2b floor. Boards: eliza-mtp, eliza-evk. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 1505342273761 | arm64: dts: qcom: eliza: add the APSS watchdog | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 8b74bf129586e | arm64: dts: qcom: eliza: add the EL2 overlay | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 65a58bc69a85b | dt-bindings: soc: qcom: geni-se: allow a second stream ID | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 4a51e8ebcd55d | arm64: dts: qcom: eliza: add the QUP 2 SE DMA stream ID | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| a28f4d7229b48 | arm64: dts: qcom: eliza-mtp: load the QUP 2 serial engine firmware | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| cf710d37a9517 | FROMLIST: arm64: dts: qcom: eliza: Add FastRPC support | Abel Vesa | FROMLIST: https://lore.kernel.org/r/20261102-dts-qcom-eliza-add-fastrpc-v5-1-27ad8db4a47d@oss.qualcomm.com |
| 7ace524637f6b | FROMLIST: dt-bindings: media: qcom,sm8550-iris: Document Eliza IRIS accelerator | Wangao Wang | FROMLIST: https://lore.kernel.org/r/20260921-add_iris_for_eliza-v2-1-5fe7be8c0230@oss.qualcomm.com |
| b3cd0781c0a2d | FROMLIST: media: iris: Add support for Eliza | Wangao Wang | FROMLIST: https://lore.kernel.org/r/20260921-add_iris_for_eliza-v2-2-5fe7be8c0230@oss.qualcomm.com |
| 5c1240d665a25 | FROMLIST: arm64: dts: qcom: eliza: Add iris video codec node | Wangao Wang | FROMLIST: https://lore.kernel.org/r/20260921-add_iris_for_eliza-v2-3-5fe7be8c0230@oss.qualcomm.com |
| 3004f3a4ef0ae | FROMLIST: arm64: dts: qcom: eliza-mtp: Enable iris video codec | Wangao Wang | FROMLIST: https://lore.kernel.org/r/20260921-add_iris_for_eliza-v2-4-5fe7be8c0230@oss.qualcomm.com |
| 9c40d517327e9 | FROMLIST: arm64: dts: qcom: eliza-evk: Enable iris video codec | Wangao Wang | FROMLIST: https://lore.kernel.org/r/20260921-add_iris_for_eliza-v2-5-5fe7be8c0230@oss.qualcomm.com |
| d3cde3c95ff59 | arm64: dts: qcom: eliza: use four interrupt cells for the GIC | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 1bbc769dcceff | arm64: dts: qcom: eliza: add the CPU PMUs | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 752cc0f3a2c07 | arm64: dts: qcom: eliza-el2: add the ADSP LPASS island stream | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 1e3f0cbfdd1b0 | arm64: dts: qcom: eliza: add the LPASS RX and TX macros | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 5be5357b178a9 | arm64: dts: qcom: eliza-mtp: enable audio | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| ed5848ea786ab | arm64: dts: qcom: eliza-mtp: route the headset through the USB-C port | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 2e8dce67db492 | arm64: dts: qcom: eliza-mtp: load the QUPv3 1 firmware for I2C 4 | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 36d4682860e7c | clk: qcom: camcc-eliza: Declare icc path dependency for TITAN_TOP_GDSC | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 6903a4fbdd858 | arm64: dts: qcom: eliza: Add interconnects to camcc | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 54fe7d0f7b0b1 | arm64: dts: qcom: eliza-mtp: raise the vreg_l2b floor to 880 mV | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/pakala

SM8750 MTP: EL2 overlay, ADSP LPASS island stream, QUP firmware, iris at EL2. Boards: sm8750-mtp. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| f7a10f0fb74bc | arm64: dts: qcom: sm8750: add the EL2 overlay | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| f4f88b48eebd2 | arm64: dts: qcom: sm8750-el2: add the ADSP LPASS island stream | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| aef0e65a2defb | arm64: dts: qcom: sm8750-mtp: load the QUPv3 1 firmware for I2C 5 | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| d0cc6e810ac8b | arm64: dts: qcom: sm8750-el2: enable iris with its firmware stream | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/hamoa

X1E (Hamoa): x1-el2 ADSP LPASS island stream and iris firmware stream. Boards: hamoa-iot-evk (x1-el2). On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 0a801f798a976 | arm64: dts: qcom: x1-el2: add the ADSP LPASS island stream | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 2bc39ce00f2eb | arm64: dts: qcom: x1-el2: describe the iris firmware stream | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/purwa

X1P42100 (Purwa): QUP firmware name, GPU SMMU vote clock. Boards: purwa-iot-evk. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 60c685b88c020 | arm64: dts: qcom: purwa-iot-som: name the QUP firmware | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| ede4061c10fb5 | clk: qcom: gpucc-x1p42100: add the HLOS GPU SMMU vote clock | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/kaanapali

Kaanapali MTP: EL2 overlay with DSP streams, iris node (FROMLIST) and firmware stream. Boards: kaanapali-mtp. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 6c015708b73a7 | arm64: dts: qcom: kaanapali: add the EL2 overlay | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| a26cec12ec073 | arm64: dts: qcom: kaanapali: add the DSP SMMU streams to the EL2 overlay | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 10074188bba94 | FROMLIST: arm64: dts: qcom: kaanapali: add iris video node | Vikash Garodia | FROMLIST: https://lore.kernel.org/r/20261001-kaanapali-iris-v4-5-642f9ac5e699@oss.qualcomm.com |
| 5d36bc0618c8f | FROMLIST: arm64: dts: qcom: kaanapali-mtp: enable iris video node on mtp board | Vikash Garodia | FROMLIST: https://lore.kernel.org/r/20261001-kaanapali-iris-v4-6-642f9ac5e699@oss.qualcomm.com |
| 98fb40b4368be | arm64: dts: qcom: kaanapali: add the iris firmware stream at EL2 | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/glymur

Glymur (also Mahua): TZ and boot carve-outs, aggre3 QoS clocks, GPU zap, eDP HPD, USB GDSCs on the QMP PHYs. Boards: glymur-crd, mahua-crd. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 1673cf3ddafa6 | arm64: dts: qcom: glymur: reserve the TZ carve-outs | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| b0ff2d6bb5cdb | arm64: dts: qcom: glymur: reserve the boot firmware carve-outs | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 3a61cd527409b | dt-bindings: interconnect: qcom,glymur-rpmh: add aggre3 NoC clocks | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| b5a9dde3abb9e | interconnect: qcom: glymur: require clocks for the aggre3 NoC QoS | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| a3ab46b120aba | arm64: dts: qcom: glymur: add the aggre3 NoC QoS clocks | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 6fac532375630 | arm64: dts: qcom: glymur: add the GPU zap shader node | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 7f4707ecc91e3 | arm64: dts: qcom: glymur-crd: name the GPU zap shader firmware | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 1b8e1c4ec32ce | arm64: dts: qcom: glymur-crd: mux the eDP HPD pin | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| be0b4c9c20aea | arm64: dts: qcom: glymur: add the USB controller GDSCs to the QMP PHYs | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/talos

QCS615 Ride: TZ/AOP reservations, EL2 virtual timer. Boards: qcs615-ride. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 24f46ae33213b | arm64: dts: qcom: talos: Reserve the TZ and AOP memory regions | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 72fa3afd712e7 | arm64: dts: qcom: talos-el2: add the EL2 virtual timer interrupt | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/monaco

QCS8300 (Monaco) VENTUNO Q: carve-outs, 4-cell GIC + PMU PPI partitions, EL2 vtimer, RTC, codec IRQ, iris at EL2, two board HACKs. Boards: monaco-arduino-monza. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| 0d7e9dc2594e2 | arm64: dts: qcom: monaco: reserve firmware carve-outs | Tyler Baker | posted by Tyler Baker 2026-10-25 [PATCH], 2 replies: https://lore.kernel.org/r/20261024-monaco-fw-carveouts-v1-1-1cd68aedf46d@oss.qualcomm.com |
| 734295d80f382 | arm64: dts: qcom: monaco: switch the GIC to 4 interrupt cells | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 6e248a80038b1 | arm64: dts: qcom: monaco: add PPI partitions for the PMUs | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| b5dd130f11a44 | arm64: dts: qcom: monaco-el2: add the EL2 virtual timer interrupt | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 10f92e87f0eb3 | arm64: dts: qcom: monaco-arduino-monza: keep the RTC offset in RTCInfo | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| e97a45b1e7373 | arm64: dts: qcom: monaco-arduino-monza: fix the codec interrupt type | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| e17f3f6ce59c2 | arm64: dts: qcom: Add EL2 support for Iris for monaco | Gourav Kumar | not posted (lei, all lists, 2026-11-04) |
| a2420a3fa6adf | HACK: arm64: dts: qcom: monaco-arduino-monza: disable L1SS on PCIe0 | Tyler Baker | HACK, not for upstream as is |
| 0087324991eee | HACK: PCI: Mask Training Error for Pericom PI7C9X2G304 switch | Tyler Baker | HACK, not for upstream as is |

## qcom/lemans

SA8775P (Lemans) EVK: ETM peripheral ID, EL2 vtimer, RTC. Boards: lemans-evk. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| fac6f2315e3a0 | arm64: dts: qcom: lemans: describe the ETM peripheral ID | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| efa3738eeeaf0 | arm64: dts: qcom: lemans-el2: add the EL2 virtual timer interrupt | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 105f44cd11e62 | arm64: dts: qcom: lemans-evk: allow setting the RTC | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/kodiak

QCS6490 RB3 Gen 2: EL2 overlay (FROMLIST), 4-cell GIC + PMU PPI partitions, EL2 vtimer, RTC, venus firmware node. Boards: qcs6490-rb3gen2. On: next-20261103.

Adaptation: qcom/kodiak, "arm64: dts: qcom: kodiak-el2: add the EL2 virtual timer interrupt" on the v11 kodiak EL2 overlay (Mukesh Ojha): v11 ends with a &wifi wifi-firmware stream; the timer override goes after it.

| commit | subject | author | upstream status |
|---|---|---|---|
| 389ccff37f5a3 | FROMLIST: arm64: dts: qcom: qcm6490-idp/qcs6490-rb3gen2: Keep wlan_ce_mem and drop redundant wifi memory-region | Mukesh Ojha | FROMLIST: https://lore.kernel.org/r/20260803121042.3313824-2-mukesh.ojha@oss.qualcomm.com |
| 9eacce59cfc16 | FROMLIST: arm64: dts: qcom: kodiak: Add EL2 overlay | Mukesh Ojha | FROMLIST: https://lore.kernel.org/r/20260803121042.3313824-3-mukesh.ojha@oss.qualcomm.com |
| 5769194c71444 | arm64: dts: qcom: kodiak: switch the GIC to 4 interrupt cells | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| bd8cd38023684 | arm64: dts: qcom: kodiak: add PPI partitions for the PMUs | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| aac0d567d8c4d | arm64: dts: qcom: kodiak-el2: add the EL2 virtual timer interrupt | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| bf75e549d019e | arm64: dts: qcom: qcs6490-rb3gen2: allow setting the RTC | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| ec2df5809a4d0 | arm64: dts: qcom: kodiak-el2: Add venus firmware sub node | Vishnu Reddy | not posted (lei, all lists, 2026-11-04) |

## qcom/agatti

QRB2210 UNO Q: EL2 overlay, zap shader stream, video firmware stream for iris. Boards: qrb2210-arduino-imola. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| cbc51b85c4187 | arm64: dts: qcom: agatti: add EL2 overlay for the UNO Q | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 2b4288dea7c56 | arm64: dts: qcom: agatti-el2: describe the zap shader stream | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| bef938b89814e | arm64: dts: qcom: agatti-el2: describe the video firmware stream | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/nord

Nord IQ-10 EVK on the upstream nord-embedded.dtsi: EL2 virtual timer PPI, board binding and board DT (console, UFS, regulators, QUP). Boards: iq10-evk. On: next-20261103.

| commit | subject | author | upstream status |
|---|---|---|---|
| b818708ec4082 | arm64: dts: qcom: nord: add the EL2 virtual timer interrupt | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 5d087a1637b9b | dt-bindings: arm: qcom: document the IQ-10 EVK | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 2ad528c7393d4 | arm64: dts: qcom: add the IQ-10 EVK board | Shawn Guo | not posted (lei, all lists, 2026-11-04) |
| d59c27fc53b3c | arm64: defconfig: enable the Nord interconnect driver | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

## qcom/eliza-camera

Eliza MTP camera, kept out of qcom/integration: CAMSS (Hangxiang Ma Kaanapali v24 and SM8750 v6, our Eliza CAMSS), IMX766/IMX858 sensors and the camera DT. On qcom/eliza. Boards: eliza-mtp camera overlays. On: qcom/eliza.

Adaptation: qcom/eliza-camera, "media: qcom: camss: Do not assume legacy CSIPHYs without endpoints" on camss v24: v24 8/15 now sets legacy_phy from the presence of CSIPHY resources and skips the endpoint scan without them. Kept that structure with this patch's stricter condition (resources present and the first one carrying hw_ops).

Adaptation: qcom/eliza-camera, "phy: qcom-mipi-csi2: Add Eliza support" on camss v24: v24 moved the X1E configuration after Kaanapali. Kept v24's X1E entries and added only the Eliza genpds, configuration, match entry and declaration.

Adaptation: qcom/eliza-camera, "media: qcom: camss: Add Eliza compatible": camss v24 dropped csiphy_res_kaanapali (Kaanapali uses standalone CSI2 PHY devices), but the SM8750 v6 series (written against v23) and the Eliza resources still reference it. Kept its definition next to icc_res_eliza; its entries have no hw_ops, so the v24 legacy-PHY detection still picks the standalone PHYs. Rework once SM8750 v7 follows v24.

Adaptation: qcom/eliza-camera, SM8750 camss v6 (written on camss v23): from "csiphy: Add support for v2.3.0 two-phase CSIPHY" on, the SM8750 resources use csiphy_res_kaanapali, which camss v24 removed. Adapted (adapt/8b35d8880101.sh): the table is defined in that commit, its first user, so every commit builds with W=1 clean, and the Eliza compatible commit no longer adds it. Its entries have no hw_ops, so v24's legacy-PHY detection picks the standalone PHYs.

| commit | subject | author | upstream status |
|---|---|---|---|
| 243df341f7061 | FROMLIST: dt-bindings: phy: qcom,x1e80100-csi2-phy: Add Kaanapali CSI2 PHY | Hangxiang Ma | FROMLIST: https://lore.kernel.org/r/20261102-kaanapali-camss-v24-1-d6b8f3f6f66f@oss.qualcomm.com |
| b79f1364ec7bc | FROMLIST: phy: qcom-mipi-csi2: Parametrise the common status register offset | Hangxiang Ma | FROMLIST: https://lore.kernel.org/r/20261102-kaanapali-camss-v24-2-d6b8f3f6f66f@oss.qualcomm.com |
| e276722205368 | FROMLIST: phy: qcom-mipi-csi2: Add Kaanapali support | Hangxiang Ma | FROMLIST: https://lore.kernel.org/r/20261102-kaanapali-camss-v24-3-d6b8f3f6f66f@oss.qualcomm.com |
| 9fd5fe24aff73 | FROMLIST: media: qcom: camss: Add addr_update hooks and guard CSID reg_update | Hangxiang Ma | FROMLIST: https://lore.kernel.org/r/20261102-kaanapali-camss-v24-4-d6b8f3f6f66f@oss.qualcomm.com |
| c115ce05b309f | FROMLIST: media: dt-bindings: Add CAMSS device for Kaanapali | Hangxiang Ma | FROMLIST: https://lore.kernel.org/r/20261102-kaanapali-camss-v24-5-d6b8f3f6f66f@oss.qualcomm.com |
| f71fac3378aa0 | FROMLIST: media: qcom: camss: Add Kaanapali compatible | Hangxiang Ma | FROMLIST: https://lore.kernel.org/r/20261102-kaanapali-camss-v24-6-d6b8f3f6f66f@oss.qualcomm.com |
| ee154b7a8a5a6 | FROMLIST: media: qcom: camss: Add PM clock definitions for Kaanapali | Hangxiang Ma | FROMLIST: https://lore.kernel.org/r/20261102-kaanapali-camss-v24-7-d6b8f3f6f66f@oss.qualcomm.com |
| a550355eb9575 | FROMLIST: media: qcom: camss: Allow CSI2 PHY devices without CSIPHY resources | Vladimir Zapolskiy | FROMLIST: https://lore.kernel.org/r/20261102-kaanapali-camss-v24-8-d6b8f3f6f66f@oss.qualcomm.com |
| 9a8c4a573a577 | FROMLIST: media: qcom: camss: csid: Add support for CSID 1080 | Hangxiang Ma | FROMLIST: https://lore.kernel.org/r/20261102-kaanapali-camss-v24-9-d6b8f3f6f66f@oss.qualcomm.com |
| e24e373933665 | FROMLIST: media: qcom: camss: vfe: Add support for VFE 1080 | Hangxiang Ma | FROMLIST: https://lore.kernel.org/r/20261102-kaanapali-camss-v24-10-d6b8f3f6f66f@oss.qualcomm.com |
| 3c684f86ca282 | FROMLIST: media: qcom: camss: tpg: Add support for v2.4.0 TPG | Hangxiang Ma | FROMLIST: https://lore.kernel.org/r/20261102-kaanapali-camss-v24-11-d6b8f3f6f66f@oss.qualcomm.com |
| effc59869ae9b | FROMLIST: dt-bindings: phy: qcom,x1e80100-csi2-phy: Add SM8750 CSI2 PHY | Hangxiang Ma | FROMLIST: https://patch.msgid.link/20260721-add-support-for-camss-on-sm8750-v6-1-a5f841061d18@oss.qualcomm.com |
| 5837f2025fe37 | FROMLIST: media: dt-bindings: Add CAMSS device for SM8750 | Hangxiang Ma | FROMLIST: https://patch.msgid.link/20260721-add-support-for-camss-on-sm8750-v6-2-a5f841061d18@oss.qualcomm.com |
| e67aa9e2a885c | FROMLIST: media: qcom: camss: Add SM8750 compatible camss driver | Hangxiang Ma | FROMLIST: https://patch.msgid.link/20260721-add-support-for-camss-on-sm8750-v6-3-a5f841061d18@oss.qualcomm.com |
| 69c8176057205 | FROMLIST: media: qcom: camss: csiphy: Add support for v2.3.0 two-phase CSIPHY | Hangxiang Ma | FROMLIST: https://patch.msgid.link/20260721-add-support-for-camss-on-sm8750-v6-4-a5f841061d18@oss.qualcomm.com |
| e0efeaeff92e9 | FROMLIST: media: qcom: camss: csid: Add support for CSID 980 | Hangxiang Ma | FROMLIST: https://patch.msgid.link/20260721-add-support-for-camss-on-sm8750-v6-5-a5f841061d18@oss.qualcomm.com |
| dfbaefc9a4198 | FROMLIST: media: qcom: camss: vfe: Add support for VFE 980 | Hangxiang Ma | FROMLIST: https://patch.msgid.link/20260721-add-support-for-camss-on-sm8750-v6-6-a5f841061d18@oss.qualcomm.com |
| 5d65740eebfda | FROMLIST: media: qcom: camss: tpg: Add support for v2.3.0 TPG | Hangxiang Ma | FROMLIST: https://patch.msgid.link/20260721-add-support-for-camss-on-sm8750-v6-7-a5f841061d18@oss.qualcomm.com |
| c087756386c37 | media: qcom: camss: Do not assume legacy CSIPHYs without endpoints | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| a1cfc65969398 | dt-bindings: phy: qcom,x1e80100-csi2-phy: Add Eliza CSI2 PHY | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| c2c89885fdf1a | phy: qcom-mipi-csi2: Add Eliza support | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 127e6311ebaf0 | media: dt-bindings: Add CAMSS device for Eliza | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| b3c790379f9a6 | media: qcom: camss: Add Eliza compatible | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 994fd617c9086 | media: qcom: camss: Add Eliza CSIPHY, TPG and CSID resources | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 36653c2205632 | media: qcom: camss: vfe: Add support for VFE 970 | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 0129a721e63bc | dt-bindings: i2c: qcom-cci: Document Eliza compatible | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 1af3070bf96ab | dt-bindings: media: i2c: Add Sony IMX766 | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 367685fead557 | media: i2c: Add Sony IMX766 image sensor driver | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 4d1e703a4d3e1 | dt-bindings: media: i2c: add Sony IMX858 | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 924ac0b348d72 | WIP media: i2c: Add driver for Sony IMX858 sensor | Luca Weiss | WIP, not for upstream as is |
| 48055c1a79c7a | media: i2c: imx858: fix power-on reset/clock ordering | Marcus Ramberg | not posted (lei, all lists, 2026-11-04) |
| 56b7865e74206 | media: i2c: imx858: hold the sensor in reset until power-on | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 8783b9df1c8a4 | media: i2c: imx858: add test pattern control | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 8e6260f7eac20 | arm64: dts: qcom: eliza: Add CAMSS and CSIPHY block definitions | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 491e74d5649a4 | arm64: dts: qcom: eliza: Add CCI controller nodes | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 7246a69b6b525 | arm64: dts: qcom: eliza: Add camera MCLK pinctrl | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 45f7b757fa08d | arm64: dts: qcom: eliza-mtp: Add PM8010 regulators | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| fdfef99b4ce10 | arm64: dts: qcom: eliza-mtp: Add IMX766 rear camera overlay | Tyler Baker | not posted (lei, all lists, 2026-11-04) |
| 5e3b26b49ac22 | arm64: dts: qcom: eliza-mtp: add IMX858 rear camera overlay | Tyler Baker | not posted (lei, all lists, 2026-11-04) |

