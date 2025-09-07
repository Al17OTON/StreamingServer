#pragma once

#include <iostream>
#include <coroutine>

class CoroutineTask {
public:
    struct PromiseType
    {
        CoroutineTask GetReturnObject()
        {
            return CoroutineTask{ std::coroutine_handle<PromiseType>::from_promise(*this)};
        }
        auto InitialSuspend() {return std::suspend_always{};}
        auto ReturnVoid()   {return std::suspend_never{};}
        auto FinalSuspend() {return std::suspend_always{};}
        void UnhandledException() {std::exit(1);}
    };

    std::coroutine_handle<PromiseType> co_handler;

    CoroutineTask(std::coroutine_handle<PromiseType> handler) : co_handler(handler)
    {

    }

    ~CoroutineTask()
    {
        if(true == (bool)co_handler) {
            co_handler.destroy();
        }
    }
};