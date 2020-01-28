#pragma once

extern "C" {
#include <dlt_client.h>
}

namespace DltTools {

class MessagePtr {

private:
    class Allocator : public std::allocator<DltMessage> {
        typedef std::allocator<DltMessage> super;
    public:
        void construct(DltMessage *ptr) {
            super::construct(ptr);
            dlt_message_init(ptr,0);
        }

        void destroy(DltMessage *ptr) {
            dlt_message_free(ptr,0);
            super::destroy(ptr);
        }
    };

public:
    MessagePtr(DltMessage *ptr = nullptr)
        : _raw_ptr(ptr) {
        if( _raw_ptr == nullptr ) {
            _shm_ptr = std::allocate_shared<DltMessage>(_sAllocator);
        }
    }

    operator DltMessage * () {
        return (_shm_ptr) ? _shm_ptr.get() : _raw_ptr;
    }

    DltMessage * operator->() {
        return (_shm_ptr) ? _shm_ptr.get() : _raw_ptr;
    }

private:
    DltMessage * _raw_ptr;
    std::shared_ptr<DltMessage> _shm_ptr;
    static const Allocator _sAllocator;
};

}
