/**
 * @file llinterface.h
 * @brief LLInterface is like LLMediaPlugin, yet this uses a custom connection for now
 *
 * License: GNU Public License version 2, or any later version of the GNU Public License.
 *          http://www.gnu.org/licenses/gpl-2.0.html
 * Copyright (C) 2009 Ballard, Jonathan H. <dzonatas@dzonux.net>
 */
#include "lliosocket.h"
#ifndef LL_INTERFACE_H
#define LL_INTERFACE_H

namespace Snowglobe
{
	namespace Interface
	{
		class Channel
		{
			LLSocket::ptr_t   mSocket;
			LLHost            mHost;
			bool              mConnected;
		public:
			bool connected()  { return mConnected; }
			void connect( const LLSD& info ) ;
			void reset() ;
			bool writeBuffer( const char* buffer, int length ) ;
			Channel() ;
		};
		//
		//
		extern Channel channel;
		//
		extern void init(LLPumpIO* pump);
		extern void startup();
		extern void statusLoginComplete();
		extern void update();

		class Packet : public LLSD
		{
		public:
			Packet( std::string klass, std::string method ) ;
			Packet( ) {}
			void  send() ;
			S32   parse( std::string& data ) ;
		};
	}
}


#endif //LL_INTERFACE_H
