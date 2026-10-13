// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

// Only tint-analysis calls buffer reports; the existing logger remains the sink.
namespace TintReports {
inline bool RetainedFailure(std::string_view text) {
    return text.find("FAIL")!=text.npos || text.find("failed")!=text.npos ||
        text.find("aborted")!=text.npos || text.find("unsupported")!=text.npos;
}
template<class Builder,class Sink>
void Diagnostic(bool enabled,Builder&& builder,Sink&& sink) {
    if(enabled)sink(builder());
}
template<class Writer> class Batch {
    inline static thread_local Batch* current=nullptr;
    std::string bytes;
    unsigned messages=0;
    bool owner=false;
public:
    explicit Batch(bool enabled) {
        if(enabled && !current) { current=this;owner=true; }
    }
    Batch(const Batch&)=delete;
    Batch& operator=(const Batch&)=delete;
    ~Batch() {
        if(!owner)return;
        current=nullptr;
        if(!bytes.empty())Writer::Write(bytes,messages);
    }
    static bool Append(std::string_view text) {
        if(!current)return false;
        current->bytes+="[+";
        current->bytes+=std::to_string(Writer::Timestamp());
        current->bytes+="ms] ";
        current->bytes.append(text);
        current->bytes+='\n';
        ++current->messages;
        return true;
    }
};
}
