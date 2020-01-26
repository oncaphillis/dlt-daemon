#include <iostream>
#include <cstring>
#include <iomanip>

extern "C" {
#include <dlt_client.h>
#include <dlt_user.h>
}

class Message
    : public DltMessage {
public:
    Message() {
        dlt_message_init(this,0);
    }

    ~Message() {
        dlt_message_free(this,0);
    }
private:
};

class AsHex {
public:
    AsHex(const char * ptr,size_t n)
        : _chr(ptr)
        , _n(n) {
    }

    AsHex(const std::string & s)
        : AsHex(s.c_str(),s.length()) {
    }

    AsHex(const char * s)
        : AsHex(s,std::strlen(s)) {
    }

private:
    friend
    std::ostream & operator<<(std::ostream & os,const AsHex &hx);

    const char * _chr = nullptr;
    size_t _n = 0;
};

std::ostream & operator<<(std::ostream & os,const AsHex &hx) {
    std::ios_base::fmtflags f( os.flags() );

    os << "-("  << hx._n << ")-" << std::endl;
    for(size_t i=0;i<hx._n;i++) {
        os << std::setw(2) << std::hex << std::setfill('0')
           << ((int)hx._chr[i] & 0xff) << " ";

        if ( i == hx._n-1 || (i+1) % 16 == 0 ) {
            size_t n = ((i+1 )% 16 == 0) ? 16 : ((i+1) % 16);
            os << std::string(3*(16-n),' ');
            for(size_t j=0;j<n;j++) {
                char c = hx._chr[i-n+1+j];
                os << (c >=' ' && c<0x7f ? c : '.');
            }
            os << std::endl;
        }
    }
    os.flags( f );
    return os;
}

std::ostream & operator << (std::ostream & os, const DltMessage & msg) {
    os << AsHex( (char *)&msg, sizeof(DltMessage) ) << std::endl;
    if( msg.databuffer != nullptr)
        os << AsHex( (char *) msg.databuffer,msg.databuffersize ) << std::endl;

    if( msg.extendedheader != nullptr )
        os << "args=" << (int)msg.extendedheader->noar << " ";

    std::array<char,1000> a;

    os << "pl:" << dlt_message_payload(const_cast<DltMessage *>(&msg),
                                      a.data(),a.size(),DLT_OUTPUT_ASCII,0);

    os << "[" << std::string(a.data()) << "]";

    return os;
}

class IMessageSource {

public:

    typedef DltMessage message_t;

    /** @short Get the next dlt message.
     * Message stays owned by the source.
     * Next call invalidates contents of previues message.
     * */

    virtual const message_t * nextMessage() = 0;

private:

};

class NetworkMessageSource
    : public IMessageSource {

public:
    NetworkMessageSource(const std::string & host) {
        if(dlt_client_init(&_client,0) != DLT_RETURN_OK ||
           dlt_client_set_server_ip( &_client, const_cast<char *>(host.c_str()) ) != DLT_RETURN_OK ||
           dlt_client_connect(&_client,0) != DLT_RETURN_OK) {
            throw std::runtime_error("Failed to init/connect DltClient");
        }
    }

    virtual ~NetworkMessageSource() {
        dlt_client_cleanup(&_client,0);
    }

    const message_t * nextMessage() override {
        DltReceiver * rcv = &_client.receiver;

        while(true)  {

            if( dlt_receiver_receive(rcv,DLT_RECEIVE_SOCKET)  < 0 ) {
                throw std::runtime_error("dlt_receiver_receive failed");
            }

            int n = 0;

            while( dlt_message_read(&_message,
                             (unsigned char *)(rcv->buf),rcv->bytesRcvd, 0,0) >=0 ) {

                if (_message.found_serialheader) {
                    if (dlt_receiver_remove(rcv,
                                            _message.headersize + _message.datasize - sizeof(DltStorageHeader) +
                                            sizeof(dltSerialHeader)) ==
                        DLT_RETURN_ERROR) {
                        /* Return value ignored */
                        return nullptr;
                    }
                }
                else if (dlt_receiver_remove(rcv,
                                             _message.headersize +
                                             _message.datasize -
                                             sizeof(DltStorageHeader)) ==
                         DLT_RETURN_ERROR) {
                    return nullptr;
                }

                std::cerr << "  @:" << n++ << " >> ------------ " << std::endl
                          << _message << std::endl
                          << " << ------- " << std::endl;
            }

            if (dlt_receiver_move_to_begin(rcv) == DLT_RETURN_ERROR) {
                /* Return value ignored */
                return nullptr;
            }
        }
        return &_message;
    }

private:
    DltClient _client;
    Message _message;
};


int main(int argc, char **argv) {
    NetworkMessageSource src("localhost");
    src.nextMessage();
}
