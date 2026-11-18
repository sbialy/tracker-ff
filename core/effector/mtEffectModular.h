#ifndef EFFECTOR_MTEFFECTMODULAR_H_
#define EFFECTOR_MTEFFECTMODULAR_H_

#include "mtEffect.h"
#include "tracker_plugin.h"

class mtEffectModular : public mtEffect
{
  public:
    mtEffectModular();
    virtual ~mtEffectModular() = default;

    void configurePlugin(const PtfxHeader& hdr,
                         void* scratchMem,
                         uint32_t maxScratchSize,
                         const PtfxHostApi* hostApi,
                         PtfxInitFunc initFunc,
                         PtfxSetParamFunc setParamFunc,
                         PtfxProcessBlockFunc processBlockFunc);

    virtual void setParameter(float& value, int idx) override;
    virtual bool startProcess() override;
    virtual int32_t processBlock() override;
    virtual bool getProcessState() override;
    virtual uint32_t getExpectedProcessLength() override;

    void onApplyCommitted(int instrumentIdx);

    uint16_t getFlags() const { return m_flags; }
    bool hasPlugin() const { return m_processBlockFunc != nullptr; }
    void setTargetLength(uint32_t len) { m_targetLength = len; }
    void setExtraSamples(uint32_t prependSamples, uint32_t appendSamples)
    {
        m_prependSamples = prependSamples;
        m_appendSamples  = appendSamples;
    }
    void setLoadedData(bool loaded) { processing.isLoadedData = loaded; }

  private:
    uint16_t m_flags = 0;
    uint32_t m_targetLength = 44100;
    uint32_t m_prependSamples = 0;
    uint32_t m_appendSamples = 0;
    uint32_t m_inputLength = 0;
    uint32_t m_length = 0;
    uint32_t m_processedSamples = 0;
    bool     m_state = false;
    float    m_paramValues[12] = { 0.0f };
    const int16_t* m_srcAddr[kChannels] = { nullptr };
    int16_t*       m_dstAddr[kChannels] = { nullptr };
    void*          m_scratchMem = nullptr;
    uint32_t       m_scratchSize = 0;
    const PtfxHostApi* m_hostApi = nullptr;

    PtfxInitFunc         m_initFunc = nullptr;
    PtfxSetParamFunc     m_setParamFunc = nullptr;
    PtfxProcessBlockFunc m_processBlockFunc = nullptr;
};

extern mtEffectModular sampleEditorModular;

#endif /* EFFECTOR_MTEFFECTMODULAR_H_ */
