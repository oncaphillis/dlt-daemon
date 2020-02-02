#include <iostream>
#include <cstring>
#include <iomanip>
#include <memory>
#include <queue>
#include <set>
#include <algorithm>
#include <fstream>

#include <experimental/filesystem>

#include <dlt-tools/messageptr.hpp>

namespace fs = std::experimental::filesystem;

namespace DltTools {

const MessagePtr::Allocator MessagePtr::_sAllocator;

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

        while( _queue.empty() )  {

            if( dlt_receiver_receive(rcv,DLT_RECEIVE_SOCKET)  < 0 ) {
                throw std::runtime_error("dlt_receiver_receive failed");
            }

            MessagePtr mptr;
            while( dlt_message_read(mptr,
                             (unsigned char *)(rcv->buf),rcv->bytesRcvd, 0,0) >=0 ) {

                if (mptr->found_serialheader) {
                    if (dlt_receiver_remove(rcv,
                                            mptr->headersize + mptr->datasize - sizeof(DltStorageHeader) +
                                            sizeof(dltSerialHeader)) ==
                        DLT_RETURN_ERROR) {
                        /* Return value ignored */
                        return nullptr;
                    }
                }
                else if (dlt_receiver_remove(rcv,
                                             mptr->headersize +
                                             mptr->datasize -
                                             sizeof(DltStorageHeader)) ==
                         DLT_RETURN_ERROR) {
                    return nullptr;
                }
                _queue.push(mptr);
            }

            if (dlt_receiver_move_to_begin(rcv) == DLT_RETURN_ERROR) {
                /* Return value ignored */
                return nullptr;
            }
        }

        auto m = _queue.front();
        _queue.pop();
        return m;
    }

private:
    DltClient _client;
    std::queue<MessagePtr> _queue;
};

}

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

class Downloader {
public:
    Downloader(const std::string & basename,bool auto_finish=false)
        : _finished(false)
        , _auto_finish(auto_finish) {

        auto real_name = basename;
        auto prefix = real_name;
        auto postfix = std::string("");
        auto temp_name = real_name+".tmp";

        auto idx = real_name.rfind('.');

        if( idx != real_name.npos ) {
            prefix = real_name.substr(0,idx);
            postfix = real_name.substr(idx);
        }

        int n=0;

        while( fs::exists(real_name) || fs::exists(temp_name) ) {
            std::stringstream ss;
            ss << "("  << ++n << ")";
            real_name = prefix+ss.str()+postfix;
            temp_name = real_name+".tmp";
        }
        _real_name = real_name;
        _temp_name = temp_name;

        _rstream = std::make_shared<std::ofstream>(_real_name);
        _ostream = std::make_shared<std::ofstream>(_temp_name);

        if( ! *_rstream ||  ! *_ostream ) {
            std::stringstream ss;
            ss <<  "Failed to create '" << _real_name
                << "' and/or '" << _temp_name << "'";
            throw std::runtime_error(ss.str());
        }
    }

    ~Downloader() {
        if(_auto_finish) {
            finish();
        } else {
            if( ! _finished ) {
                fs::remove(_real_name);
                fs::remove(_temp_name);
            }
        }
    }

    std::ostream & ostream() {
        return *_ostream;
    }

    void finish()  {
        if(!_finished) {
            if(_ostream) {
                _ostream.reset();
            }

            if(_rstream) {
                _rstream.reset();
            }
            fs::rename(_temp_name,_real_name);
            _finished =  true;
        }
    }
    const std::string & baseName() const {
        return _base_name;
    }

    const std:: string & tempName() const {
        return _temp_name;
    }

    const std::string & realName() const {
        return _real_name;
    }

private:
    bool _finished;
    bool _auto_finish;
    std::shared_ptr<std:: ostream> _ostream;
    std::shared_ptr<std:: ostream> _rstream;
    std::string _base_name;
    std::string _real_name;
    std::string _temp_name;
    };

int main(int argc, char **argv) {

    std::cerr << "H e l l o   World '" << argc << std::endl;

    if(argc==2) {
        Downloader dl(argv[1],true);
        std::cerr << " => " << dl.tempName() << std::endl;
        for(int i=0;i<1000;i++) {
            dl.ostream() << "AAAAAAAAAAAAAA" << std::endl;
        }
    }


#if 0
    DltTools::NetworkMessageSource src("localhost");
    const DltMessage *m;
    while ( (m = src.nextMessage()) != nullptr ) {
        std::cerr << *m << std::endl;
    }
#endif
}
