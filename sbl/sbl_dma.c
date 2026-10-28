#include "sega_xpt.h"
#include "sega_dma.h"
unsigned long satz_bus_address(const volatile void* pointer);

static DmaCpuPrm cpu_prm[2];
static DmaScuPrm scu_prm[3];

void DMA_CpuMemCopy1(void* dst, void* src, Uint32 cnt) { satz_dma_copy(satz_bus_address(dst), satz_bus_address(src), cnt, 1, 1, 1); }
void DMA_CpuMemCopy2(void* dst, void* src, Uint32 cnt) { satz_dma_copy(satz_bus_address(dst), satz_bus_address(src), cnt, 2, 2, 2); }
void DMA_CpuMemCopy4(void* dst, void* src, Uint32 cnt) { satz_dma_copy(satz_bus_address(dst), satz_bus_address(src), cnt, 4, 4, 4); }
void DMA_CpuMemCopy16(void* dst, void* src, Uint32 cnt) { satz_dma_copy(satz_bus_address(dst), satz_bus_address(src), cnt * 4, 4, 4, 4); }
Uint32 DMA_CpuResult(void) { return DMA_CPU_END; }
void DMA_CpuSetComPrm(DmaCpuComPrm* prm) { (void)prm; }
void DMA_CpuStop(Uint32 ch) { (void)ch; }
void DMA_CpuAllStop(void) { }
DmaCpuStatus DMA_CpuGetStatus(Uint32 ch) { (void)ch; return DMA_CPU_TE_SP; }
void DMA_CpuGetComStatus(DmaCpuComStatus* status) { status->ae = 0; status->nmif = 0; }

void DMA_CpuSetPrm(DmaCpuPrm* prm, Uint32 ch)
{
    DmaCpuPrm* p = &cpu_prm[ch & 1];
    if (prm->msk & DMA_CPU_M_SAR) p->sar = prm->sar;
    if (prm->msk & DMA_CPU_M_DAR) p->dar = prm->dar;
    if (prm->msk & DMA_CPU_M_TCR) p->tcr = prm->tcr;
    if (prm->msk & DMA_CPU_M_DM) p->dm = prm->dm;
    if (prm->msk & DMA_CPU_M_SM) p->sm = prm->sm;
    if (prm->msk & DMA_CPU_M_TS) p->ts = prm->ts;
}

static int cpu_step(Uint32 mode, int unit)
{
    if (mode == DMA_CPU_AM_ADD) return unit;
    if (mode == DMA_CPU_AM_SUB) return -unit;
    return 0;
}

void DMA_CpuStart(Uint32 ch)
{
    DmaCpuPrm* p = &cpu_prm[ch & 1];
    int unit = p->ts == DMA_CPU_1 ? 1 : p->ts == DMA_CPU_2 ? 2 : p->ts == DMA_CPU_16 ? 16 : 4;
    Uint32 units = unit == 16 ? p->tcr : p->tcr;
    if (unit == 16) {
        satz_dma_copy(p->dar, p->sar, units * 4, 4, cpu_step(p->sm, 4), cpu_step(p->dm, 4));
        return;
    }
    satz_dma_copy(p->dar, p->sar, units, unit, cpu_step(p->sm, unit), cpu_step(p->dm, unit));
}

void DMA_ScuInit(void) { }
void DMA_ScuMemCopy(void* dst, void* src, Uint32 cnt) { satz_dma_copy(satz_bus_address(dst), satz_bus_address(src), cnt, 1, 1, 1); }
Uint32 DMA_ScuResult(void) { return DMA_SCU_END; }
void DMA_ScuGetStatus(DmaScuStatus* status, Uint32 ch) { (void)ch; status->dxmv = DMA_SCU_NO_MV; }

void DMA_ScuSetPrm(DmaScuPrm* prm, Uint32 ch)
{
    DmaScuPrm* p = &scu_prm[ch % 3];
    if (prm->msk & DMA_SCU_M_DXR) p->dxr = prm->dxr;
    if (prm->msk & DMA_SCU_M_DXW) p->dxw = prm->dxw;
    p->dxc = prm->dxc;
    p->dxad_r = prm->dxad_r;
    p->dxad_w = prm->dxad_w;
    p->dxmod = prm->dxmod;
}

void DMA_ScuStart(Uint32 ch)
{
    DmaScuPrm* p = &scu_prm[ch % 3];
    int src_step = p->dxad_r == DMA_SCU_R0 ? 0 : 4;
    int dst_step = p->dxad_w == DMA_SCU_W0 ? 0 : 4;
    satz_dma_copy(p->dxw, p->dxr, p->dxc / 4, 4, src_step, dst_step);
    if (p->dxc & 3)
        satz_dma_copy(p->dxw + (p->dxc & ~3u), p->dxr + (src_step ? (p->dxc & ~3u) : 0), p->dxc & 3, 1, src_step ? 1 : 0, 1);
}
