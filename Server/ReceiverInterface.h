#pragma once
#include <msquic.h>

class ReceiverInterface {
public:
    virtual long int Read(int64_t stream_id, QUIC_STREAM_EVENT* event) = 0;
};