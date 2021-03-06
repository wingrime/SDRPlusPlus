#include <imgui.h>
#include <spdlog/spdlog.h>
#include <new_module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <libairspyhf/airspyhf.h>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO {
    /* Name:            */ "airspy_hf_source",
    /* Description:     */ "Airspy HF source module for SDR++",
    /* Author:          */ "Ryzerth",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

class AirspyHFSourceModule : public ModuleManager::Instance {
public:
    AirspyHFSourceModule(std::string name) {
        this->name = name;

        sampleRate = 2560000.0;

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;
        sigpath::sourceManager.registerSource("Airspy HF+", &handler);
    }

    ~AirspyHFSourceModule() {
        
    }

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:
    static void menuSelected(void* ctx) {
        AirspyHFSourceModule* _this = (AirspyHFSourceModule*)ctx;
        core::setInputSampleRate(_this->sampleRate);
        spdlog::info("AirspyHFSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        AirspyHFSourceModule* _this = (AirspyHFSourceModule*)ctx;
        spdlog::info("AirspyHFSourceModule '{0}': Menu Deselect!", _this->name);
    }
    
    static void start(void* ctx) {
        AirspyHFSourceModule* _this = (AirspyHFSourceModule*)ctx;
        if (_this->running) {
            return;
        }
        // DO START HERE
        _this->running = true;
        _this->workerThread = std::thread(worker, _this);
        spdlog::info("AirspyHFSourceModule '{0}': Start!", _this->name);
    }
    
    static void stop(void* ctx) {
        AirspyHFSourceModule* _this = (AirspyHFSourceModule*)ctx;
        if (!_this->running) {
            return;
        }
        // DO STOP HERE
        _this->running = false;
        _this->stream.stopWriter();
        _this->workerThread.join();
        _this->stream.clearWriteStop();
        spdlog::info("AirspyHFSourceModule '{0}': Stop!", _this->name);
    }
    
    static void tune(double freq, void* ctx) {
        AirspyHFSourceModule* _this = (AirspyHFSourceModule*)ctx;
        if (_this->running) {
            _this->client.setFrequency(freq);
        }
        _this->freq = freq;
        spdlog::info("AirspyHFSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }
    
    static void menuHandler(void* ctx) {
        AirspyHFSourceModule* _this = (AirspyHFSourceModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvailWidth();
        float portWidth = ImGui::CalcTextSize("00000").x + 20;

        ImGui::SetNextItemWidth(menuWidth - portWidth);
        ImGui::InputText(CONCAT("##_ip_select_", _this->name), _this->ip, 1024);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(portWidth);
        ImGui::InputInt(CONCAT("##_port_select_", _this->name), &_this->port, 0);

        ImGui::SetNextItemWidth(ImGui::CalcTextSize("OOOOOOOOOO").x);
        if (ImGui::Combo("Direct sampling", &_this->directSamplingMode, "Disabled\0I branch\0Q branch\0")) {
            if (_this->running) {
                _this->client.setDirectSampling(_this->directSamplingMode);
            }
        }

        if (ImGui::Checkbox("RTL AGC", &_this->rtlAGC)) {
            if (_this->running) {
                _this->client.setAGCMode(_this->rtlAGC);
            }
        }

        if (ImGui::Checkbox("Tuner AGC", &_this->tunerAGC)) {
            if (_this->running) {
                _this->client.setGainMode(!_this->tunerAGC);
                if (!_this->tunerAGC) {
                    _this->client.setGainIndex(_this->gain);
                }
            }
        }

        if (_this->tunerAGC) { style::beginDisabled(); }
        ImGui::SetNextItemWidth(menuWidth);
        if (ImGui::SliderInt(CONCAT("##_gain_select_", _this->name), &_this->gain, 0, 29, "")) {
            if (_this->running) {
                _this->client.setGainIndex(_this->gain);
            }
        }
        if (_this->tunerAGC) { style::endDisabled(); }
    }

    static void worker(void* ctx) {
        AirspyHFSourceModule* _this = (AirspyHFSourceModule*)ctx;
        int blockSize = _this->sampleRate / 200.0;
        uint8_t* inBuf = new uint8_t[blockSize * 2];

        while (true) {
            // Read samples here
            _this->client.receiveData(inBuf, blockSize * 2);
            if (_this->stream.aquire() < 0) { break; }
            for (int i = 0; i < blockSize; i++) {
                _this->stream.data[i].q = ((double)inBuf[i * 2] - 128.0) / 128.0;
                _this->stream.data[i].i = ((double)inBuf[(i * 2) + 1] - 128.0) / 128.0;
            }
            _this->stream.write(blockSize);
        }

        delete[] inBuf;
    }

    std::string name;
    bool enabled = true;
    dsp::stream<dsp::complex_t> stream;
    double sampleRate;
    SourceManager::SourceHandler handler;
    std::thread workerThread;
    RTLTCPClient client;
    bool running = false;
    double freq;
    char ip[1024] = "localhost";
    int port = 1234;
    int gain = 0;
    bool rtlAGC = false;
    bool tunerAGC = false;
    int directSamplingMode = 0;
};

MOD_EXPORT void _INIT_() {
   // Do your one time init here
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new AirspyHFSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (AirspyHFSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    // Do your one shutdown here
}