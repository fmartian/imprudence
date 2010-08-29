 /**
 * @file llinterface.cpp
 * @brief LLInterface is like LLMediaPlugin, yet this uses a custom connection for now
 *
 * License: GNU Public License version 2, or any later version of the GNU Public License.
 *          http://www.gnu.org/licenses/gpl-2.0.html
 * Copyright (C) 2009 Ballard, Jonathan H. <dzonatas@dzonux.net>
 */

#include "llviewerprecompiledheaders.h"

#include "lliosocket.h"
#include "llbufferstream.h"
#include "llsdserialize.h"
#include "lliohttpserver.h"
#include "llsdutil.h"
#include "llinventorytype.h"
#include "llchat.h"
#include "llvfile.h"
#include "llvfs.h"
#include "llmultigesture.h"
#include "llhttpstatuscodes.h"
#include "llnotecard.h"
#include "llmemorystream.h"
#include "lltexteditor.h"
#include "llassetstorage.h"

#include "llagent.h"
#include "llappviewer.h"
#include "llcallingcard.h"
#include "llgesturemgr.h"
#include "llimview.h"
#include "llinterface.h"
#include "llinventorymodel.h"
#include "llstartup.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llviewerregion.h"

extern void send_chat_from_viewer(const std::string utf8_out_text, EChatType type, S32 channel) ;
void session_starter_helper( const LLUUID& temp_session_id,	const LLUUID& other_participant_id,	EInstantMessage im_type) ;
void deliver_message(const std::string& utf8_text, const LLUUID& im_session_id, const LLUUID& other_participant_id, EInstantMessage dialog) ;

namespace Snowglobe
{
	namespace Interface
	{
		//const int  VERSION_MAJOR             = 0 ;
		//const int  VERSION_MINOR             = 9 ;
		//const int  VERSION_REVISION          = 10 ;
		LLUUID     session_id ;

		bool allowConnections()
		{
			return gSavedSettings.getBOOL("InterfaceAllowConnections") && !LLAppViewer::instance()-> getSecondInstance();
		}

		bool allowConnection( LLHost remote )
		{
			if( allowConnections() && !LLAppViewer::instance()->getSecondInstance() )
			{
				if( remote.getIPString() != "127.0.0.1" )
					return gSavedSettings.getBOOL("InterfaceAllowRemoteConnections") ;
				return true ;
			}
			return false ;
		}

		static F64 sTimeLastUpdated = 0.0 ;
		static F64 sUpdateInterval  = 5.0 ;

		void update()
		{
			if (LLAppViewer::instance()->getSecondInstance())
			{
				return;
			}

			F64 current = LLTimer::getElapsedSeconds() ;
			if(  ! ( channel.connected() && current - sTimeLastUpdated > sUpdateInterval )  )
				return ;
			LLViewerRegion*  region    = gAgent.getRegion() ;
			if( NULL == region )
				return ;
			LLVector3        position  = gAgent.getPositionAgent() ;
			LLVector3d       global    = gAgent.getPositionGlobal() ;
			Packet p( "Agent", "Update" ) ;
			p["Position"]["X"]         = position.mV[0] ;
			p["Position"]["Y"]         = position.mV[1] ;
			p["Position"]["Z"]         = position.mV[2] ;
			p["GlobalPosition"]["X"]   = global.mdV[0] ;
			p["GlobalPosition"]["Y"]   = global.mdV[1] ;
			p["GlobalPosition"]["Z"]   = global.mdV[2] ;
			p["Region"]                = region->getRegionID() ;
			p.send() ;
			sTimeLastUpdated = current ;
		}

		void statusLoginComplete()
		{
			if (LLAppViewer::instance()->getSecondInstance())
			{
				return;
			}

			sTimeLastUpdated = 0.0 ;
			{
				Packet p( "Agent", "SetID" ) ;
				p["id"]                       = gAgentID;
				p.send() ;
			}
			{
				Packet p( "Interface", "Status" ) ;
				p["status"] = "login complete" ;
				p.send() ;
			}
		}

		void startup()
		{
			if (LLAppViewer::instance()->getSecondInstance())
			{
				return;
			}

			// This is called at STATE_MULTIMEDIAINIT. TODO: Maybe spawn...
			LL_INFOS("Interface::") << "interface startup" << LL_ENDL;
			if( ! channel.connected() )
				return ;
			Packet p( "Interface", "Status" ) ;
			p["status"] = "startup" ;
			p.send() ;
		}
	}
}


namespace Snowglobe
{
	using namespace std ;
	//
	namespace Interface
	{
		class ProtocolParser : public LLIOPipe
		{
			string mInput ;
			static const int CHUNK_SIZE  = 1024 ;
		protected:
			LLSocket::ptr_t  mSocket ;
			virtual EStatus process_impl( const LLChannelDescriptors& channels,	buffer_ptr_t& iobuffer, bool& eos, LLSD& context, LLPumpIO* pump)
			{
				LLBufferStream istr(channels, iobuffer.get());
				while( istr.good() )
				{
					char  buf[CHUNK_SIZE] ;
					istr.read( buf, CHUNK_SIZE );
					if( ! stream( buf, istr.gcount() ) )
						{
						//channel.reset() ;
						//return STATUS_DONE ;
						return STATUS_OK ;
						}
				}
				return STATUS_OK ;
			}
		public:
			void data( string str )
			{
				//LL_INFOS("Interface::ProtocolParser") << "========= data: " << str << LL_ENDL;
				Packet p ;
				p.parse( str ) ;
				std::string klass  = p["packet_klass"] ;
				std::string method = p["packet_method"] ;
				// klass methods go here...
				if( klass == "Agent" )
				{
					if( method == "Chat")
						{
						send_chat_from_viewer( p["text"].asString(), (EChatType)p["type"].asInteger(), p["channel"].asInteger() /* , p["animate"].asBoolean() */ ) ;  // TODO:DZ animate
						}
					else
					if( method == "StartTyping")
						gAgent.startTyping() ;
					else
					if( method == "StopTyping")
						gAgent.stopTyping() ;
				}
				else
				if( klass == "ChatterBox" )
				{
					if( method == "DeliverMessage" )
					{
					deliver_message( p["message"].asString(), p["session"].asUUID(), p["to"].asUUID(), (EInstantMessage) p["dialog"].asInteger() ) ;
					}
					else
					if( method == "SendGroupStartSession"  )
					{
						LLUUID group_id = p["session"].asUUID() ;
						LLGroupData group_data;
						gAgent.getGroupData(group_id, group_data) ;
						gIMMgr->setFloaterOpen(TRUE);
						gIMMgr->addSession(
							group_data.mName,
							IM_SESSION_GROUP_START,
							group_id);
						//session_starter_helper( p["session"].asUUID(), p["to"].asUUID(), (EInstantMessage) p["dialog"].asInteger() ) ;
						//gMessageSystem->addBinaryDataFast( _PREHASH_BinaryBucket, EMPTY_BINARY_BUCKET, EMPTY_BINARY_BUCKET_SIZE ) ;
						//gAgent.sendReliableMessage();
					}
				}
			}
			bool stream( const char* buffer, int length )
			{
				if( length == 0 )
					return false ;
				//LL_INFOS("Interface::Listener") << "stream: length=" << length << LL_ENDL;
				int start = 0 ;
				int delim ;
				while( start < length )
				{
					//LL_INFOS("Interface::Listener") << "stream: start=" << start << LL_ENDL;
					delim = start ;
					do
					{
						if( buffer[delim] == '\000' )
							break ;
					} while( ++delim < length ) ;
					//LL_INFOS("Interface::Listener") << "stream: delim=" << delim << LL_ENDL;
					if( delim == length )
					{
							mInput.append( buffer+start, length-start ) ;
							break ;
					}
					if( mInput.empty() )
					{
						if( delim != start )
							data( string( buffer+start, delim-start ) ) ;
					}
					else
					{
						if( delim != start )
							mInput.append( buffer+start, delim-start ) ;
						data( mInput ) ;
						mInput.clear() ;
					}
					start = ++delim ;
				}
				//LL_INFOS("Interface::Listener") << "stream: end" << LL_ENDL;
				return true ;
			}

		public:
			~ProtocolParser()
			{
				//LL_INFOS("Interface::Channel") << "====================================== protocol ended!!" << LL_ENDL;
				channel.reset() ;
			}
			ProtocolParser()
			{
				//LL_INFOS("Interface::ProtocolParser") << "new " << LL_ENDL;
			}
		};
	}
}

namespace Snowglobe
{
	using namespace std ;
	//
	namespace Interface
	{
		LLPumpIO*    pump ;

		void Channel::connect( const LLSD& info )
		{
			LL_INFOS("Interface::Channel") << "interface connect attempt info: " << info  << LL_ENDL;

			if( ! info.has( "port") )
				return ;
			std::string  host ;
			int          port ;

			if( info.has( "host") )
				host = info["host"].asString() ;
			else
				host = "127.0.0.1" ;

			port = info["port"].asInteger() ;

			LL_INFOS("Interface::Channel") << "interface connect attempt to: " << host << ":" << port  << LL_ENDL;

			mHost = LLHost( host, port ) ;

			if( mConnected )
				return;

			// mHost = LLHost(gSavedSettings.getString("InterfaceHost").c_str(), gSavedSettings.getU32("InterfacePort")) ;
			if(!mSocket)
				mSocket = LLSocket::create(gAPRPoolp, LLSocket::STREAM_TCP);

			mConnected = mSocket->blockingConnect(mHost);
			if(!mConnected)
				{
				reset() ;
				return ;
				}

			LLPumpIO::chain_t readChain;
			readChain.push_back(LLIOPipe::ptr_t(new LLIOSocketReader(mSocket)));
			readChain.push_back(LLIOPipe::ptr_t(new ProtocolParser()));
			pump->addChain(readChain, NEVER_CHAIN_EXPIRY_SECS);
		}

		bool Channel::writeBuffer(const char* buffer, int length)
		{
			bool result = false;
			if(mConnected)
			{
				apr_status_t err;
				apr_size_t bytes = length;

				// check return code - sockets will fail (broken, etc.)
				err = apr_socket_send(
						mSocket->getSocket(),
						buffer,
						&bytes);

				if(err == 0)
				{
					result = true;
					//LL_INFOS("Interface::Channel") << "Sent: " << string( buffer, length ) << LL_ENDL;
				}
				else
				{
					char buf[MAX_STRING];
					// Assume any socket error means something bad.  For now, just close the socket.
					LL_WARNS("Interface::Channel") << "apr error " << err << " ("<< apr_strerror(err, buf, MAX_STRING) << ") sending data to communicator daemon." << LL_ENDL;
					reset() ;
				}
			}
			return result;
		}

		void Channel::reset()
		{
			//LL_INFOS("Interface::Channel") << LL_ENDL ;
			if(mSocket)
				mSocket.reset() ;
			mConnected = false;
		}

		Channel::Channel()
		{
			mConnected = false;
		}

		Channel channel;
	}
}


namespace Snowglobe
{
	namespace Interface
	{
		Packet::Packet( std::string klass, std::string method )
		{
			(*this)["packet_klass"]  = klass ;
			(*this)["packet_method"] = method ;
			if( Interface::allowConnections() )
				LL_INFOS("Interface::Packet") << "class=" << klass << " method=" << method << LL_ENDL ;
		}

		void Packet::send()
		{
			if( ! Interface::allowConnections() )
				return ;
			std::ostringstream stream;
			LLSDSerialize::toXML( *this, stream );
			//stream << '\000' ;
			//LL_INFOS("Interface::Packet") << stream.str() << LL_ENDL ;
			std::string str = stream.str() ;
			Snowglobe::Interface::channel.writeBuffer( str.c_str(), str.size() + 1  );
		}

		S32 Packet::parse( std::string& data )
		{
			std::istringstream input( data );
			S32 parse_result = LLSDSerialize::fromXML( *this, input );
			LL_INFOS("Interface::Packet") << "class: " << (*this)["packet_klass"] << " -- method: " << (*this)["packet_method"] << " -- result: " << parse_result << LL_ENDL ;
			return parse_result ;
		}
	}
}

namespace Snowglobe
{
	class InterfaceNode : public LLHTTPNode
	{
	protected:
		static bool allowConnection( ResponsePtr response, const LLSD& context )
		{
			LLHost remote = LLHost( context[CONTEXT_REQUEST]["remote-host"].asString(), context[CONTEXT_REQUEST]["remote-port"].asInteger() ) ;
			if( ! Interface::allowConnection( remote ))
			{
				response->notFound() ;
				return false ;
			}

			if( gSavedSettings.getBOOL("InterfaceVerifySessionCookie") )
			{
				std::string cookies = context[CONTEXT_REQUEST][CONTEXT_HEADERS]["cookie"] ;
				std::string session ;
				int s = cookies.find("interfacesession=") ;
				if( s == std::string::npos )
				{
					response->notFound() ;
					return false ;
				}
				s = cookies.find("=", s ) + 1 ;
				int e = cookies.find(";", s ) ;
				if( e == std::string::npos )
					session = cookies.substr( s ) ;
				else
					session = cookies.substr( s , e-s ) ;
				LLUUID id ;
				if( ! id.set( session, false ) )
				{
					response->notFound() ;
					return false ;
				}
				if( id != Interface::session_id )
				{
					response->notFound() ;
					return false ;
				}
			}

			return true ;
		}
	};

	class InterfaceNodeUUIDQuery : public InterfaceNode
	{
	public:
		InterfaceNodeUUIDQuery( ) { }

		virtual bool validate(const std::string& wildcard, LLSD& context) const
		{
			LLUUID id ;
			if( wildcard == "s" || id.set( wildcard, false ) )
				return true ;
			return false ;
		}

		virtual bool fetch(ResponsePtr response, LLSD& info,  LLUUID& id) const
		{
				response->notFound() ;
				return false ;
		}

		virtual void get(ResponsePtr response, const LLSD& context) const
		{
			if( ! allowConnection( response, context ) )
				return ;

			LLSD wildcard = context["request"]["wildcard"]["uuid"] ;
			if( wildcard.asString() == "s" )
			{
				response->methodNotAllowed() ;
				return ;
			}

			LLSD llsd ;
			LLUUID id( wildcard.asUUID() ) ;
			if( fetch( response, llsd, id ) )
				response->result( llsd ) ;
		}

		virtual void post(ResponsePtr response, const LLSD& context, const LLSD& input ) const
		{
			if( ! allowConnection( response, context ) )
				return ;

			LLSD wildcard = context["request"]["wildcard"]["uuid"] ;
			if( wildcard.asString() != "s" )
			{
				response->methodNotAllowed() ;
				return ;
			}

			LLSD list = LLSD::emptyArray() ;
			LLSD::array_const_iterator end = input.endArray() ;
			for( LLSD::array_const_iterator iter = input.beginArray() ; iter != end ; ++iter )
			{
				LLSD llsd ;
				LLUUID id ;
				if( ! id.set( (*iter).asString() ) )
					{
					response->status( HTTP_BAD_REQUEST ) ;
					return ;
					}
				if( ! fetch( response, llsd, id ) )
					return ;
				llsd["ID"] = id ;
				list.append( llsd ) ;
			}
			response->result( list ) ;
		}
	};


	LLSD normalized_control_variable_value( LLControlVariable* cv )
	{
		switch( cv->type() )
		{
			case TYPE_U32 :
				return LLSD( cv->getValue().asInteger() ) ;
			case TYPE_S32 :
				return LLSD( cv->getValue().asInteger() ) ;
			case TYPE_F32 :
				return LLSD( cv->getValue().asReal() ) ;
			case TYPE_BOOLEAN :
				return LLSD( cv->getValue().asBoolean() ) ;
			case TYPE_STRING :
				return LLSD( cv->getValue().asString() ) ;
			default :
				break ;
		}
		return cv->getValue() ;
	}

	class NodeControlVariable : public InterfaceNode
	{
		LLControlVariable *cv ;
		std::string identifier ;
		std::string group ;
	public:
		NodeControlVariable( LLControlVariable *_cv, const std::string& name, const std::string& _group )
			: identifier(name), cv(_cv), group(_group) { }

		virtual void get(ResponsePtr response, const LLSD& context) const
		{
			if( ! allowConnection( response, context ) )
				return ;

			LL_INFOS("") << "get: /ControlGroup/" << group << "/" << identifier << LL_ENDL ;
			response->result( normalized_control_variable_value( cv ) ) ;
		}
	};

	class NodeControlGroup : public InterfaceNode
	{
		const LLControlGroup  *cg ;
	public:
		NodeControlGroup()
			: cg(NULL) {}
		NodeControlGroup( const LLControlGroup *_cg )
			: cg(_cg) {}

		static LLControlGroup* ptrControlGroup( std::string& group )
		{
			if( group == "SavedSettings")
				return &gSavedSettings ;
			else
			if( group == "SavedPerAccountSettings" )
				return &gSavedPerAccountSettings ;
			return NULL ;
		}

		static void build( LLHTTPNode& node, std::string _path, std::string group )
		{
			std::string path = _path + "/" + group ;
			LLControlGroup *cg = ptrControlGroup(group) ;
			node.addNode( path,  new NodeControlGroup( cg )  );

			struct f : public LLControlGroup::ApplyFunctor
			{
				std::string group ;
				std::string path ;
				LLHTTPNode& node;
				f( LLHTTPNode& _node, std::string _path, std::string _group )
					: node(_node), path(_path), group(_group) {}
				virtual void apply(const std::string& identifier, LLControlVariable* cv)
				{
					node.addNode( path +"/" + identifier, new NodeControlVariable(cv, identifier, group) );
				}
			} func( node, path, group );

			cg->applyToAll(&func);
		}

		static void build( LLHTTPNode& node )
		{
			std::string path = "/ControlGroup" ;
			node.addNode( path, new NodeControlGroup() );
			build( node, path, "SavedSettings") ;
			build( node, path, "SavedPerAccountSettings") ;
		}

		static void list( LLSD& response, LLControlGroup* cg )
		{
			struct f : public LLControlGroup::ApplyFunctor
			{
				LLSD& response ;
				f( LLSD& _response )
					: response(_response) {}
				virtual void apply(const std::string& identifier, LLControlVariable* cv)
				{
					LLSD llsd ;
					llsd["identifier"]               = identifier ;
					llsd["persist"]                  = cv->isPersisted() ;
					llsd["comment"]                  = cv->getComment() ;
					llsd["type"]                     = (S32) cv->type() ;
					llsd["value"]                    = normalized_control_variable_value( cv ) ;
					response.append( llsd ) ;
				}
			} func( response );

			cg->applyToAll(&func);
		}

		virtual void get(ResponsePtr response, const LLSD& context) const
		{
			if( ! allowConnection( response, context ) )
				return ;

			LLSD info = LLSD::emptyArray( );

			if( !cg )
			{
				info.append( LLSD( "SavedSettings") ) ;

				info.append( LLSD( "SavedPerAccountSettings") ) ;
				response->result( info ) ;
				return ;
			}

			list( info, const_cast<LLControlGroup*>( cg ) ) ;
			response->result( info ) ;
			return ;
		}
	};

	class NodeAgentGroups : public InterfaceNode
	{
	public:
		NodeAgentGroups( ) { }

		static void build( LLHTTPNode& node )
		{
			std::string path = "/Agent/Groups" ;
			node.addNode( path, new NodeAgentGroups() );
		}

		virtual void get(ResponsePtr response, const LLSD& context) const
		{
			if( ! allowConnection( response, context ) )
				return ;

			LL_INFOS("") << "get: /Agent/Groups" << LL_ENDL ;
			LLSD info = LLSD::emptyArray();
			S32 count = gAgent.mGroups.count() ;
			for( S32 i = 0; i < count; ++i )
				{
				LLSD g ;
				LLGroupData* llgd    = &gAgent.mGroups.get( i ) ;
				g["id"]              = llgd->mID ;
				g["insignia"]        = llgd->mInsigniaID ;
				g["powers"]          = ll_sd_from_U64( llgd->mPowers ) ;
				g["accept_notices"]  = (bool) llgd->mAcceptNotices ;
				g["list_in_profile"] = (bool) llgd->mListInProfile ;
				g["contribution"]    = llgd->mContribution ;
				g["name"]            = llgd->mName ;
				info.append( g ) ;
				}
			response->result( info ) ;
		}
	};

	class NodeAvatarTrackerFriends : public InterfaceNode
	{
	public:
	NodeAvatarTrackerFriends( ) { }

		static void build( LLHTTPNode& node )
		{
			std::string path = "/AvatarTracker/Friends" ;
			node.addNode( path, new NodeAvatarTrackerFriends() );
		}

		class f : public LLRelationshipFunctor
		{
		public:
			LLSD& response ;
			f( LLSD& _response )
				: response(_response) {}
			virtual bool operator()( const LLUUID& buddy_id, LLRelationship* buddy )
			{
				std::string first ;
				std::string last ;
				gCacheName->getName( buddy_id, first, last) ;
				response.append( buddy_id ) ;
				return true ;
			}
		};

		virtual void get(ResponsePtr response, const LLSD& context) const
		{
			if( ! allowConnection( response, context ) )
				return ;
			LL_INFOS("") << "get: /AvatarTracker/Friends" << LL_ENDL ;
			LLSD info = LLSD::emptyArray( );

			f _f( info ) ;
			LLAvatarTracker::instance().applyFunctor( _f );
			response->result( info ) ;
		}
	};

	class NodeAvatarTrackerFriend : public InterfaceNodeUUIDQuery
	{
	public:
	NodeAvatarTrackerFriend( ) { }

		static void build( LLHTTPNode& node )
		{
			std::string path = "/AvatarTracker/Friend/<uuid>" ;
			node.addNode( path, new NodeAvatarTrackerFriend() );
		}

		bool fetch(ResponsePtr response, LLSD& info,  LLUUID& id) const
		{
			const LLRelationship* p = LLAvatarTracker::instance().getBuddyInfo( id ) ;
			if( NULL == p )
				{
				response->notFound() ;
				return false ;
				}

			std::string first ;
			std::string last ;
			gCacheName->getName( id, first, last) ;

			info["ID"]            = id ;
			info["Online"]        = p->isOnline() ;
			info["First"]         = first ;
			info["Last"]          = last ;
			return true ;
		}
	};

	class NodeGestureManagerItems : public InterfaceNode
	{
	public:
		NodeGestureManagerItems( ) { }

		static void build( LLHTTPNode& node )
		{
			std::string path = "/GestureManager/Items" ;
			node.addNode( path, new NodeGestureManagerItems() );
		}

		virtual void get(ResponsePtr response, const LLSD& context) const
		{
			if( ! allowConnection( response, context ) )
				return ;
			LLSD list = LLSD::emptyArray() ;
			std::vector<LLUUID> ids ;
			gGestureManager.getItemIDs( &ids ) ;
			for(std::vector<LLUUID>::const_iterator it = ids.begin(); it < ids.end(); ++it)
			{
				list.append( *it ) ;
			}
			response->result( list ) ;
		}
	};


	class NodeGestureManagerItem : public InterfaceNodeUUIDQuery
	{
	public:
	NodeGestureManagerItem( ) { }

		static void build( LLHTTPNode& node )
		{
			std::string path = "/GestureManager/Item/<uuid>" ;
			node.addNode( path, new NodeGestureManagerItem() );
		}

		bool fetch(ResponsePtr response, LLSD& info,  LLUUID& id) const
		{
			LLViewerInventoryItem* item = gInventory.getItem( id );
			if( NULL == item || ! item->isComplete() )
			{
				response->notFound() ;
				return false ;
			}

			if(  LLInventoryType::IT_GESTURE != item->getInventoryType()  )
			{
				response->methodNotAllowed() ;
				return false ;
			}

			if( NULL == gVFS )
			{
				llerrs << "gVFS is NULL" << llendl;
				response->status( HTTP_INTERNAL_SERVER_ERROR ) ;
				return false ;
			}

			const LLUUID  asset = item->getAssetUUID() ;
			LLVFile file( gVFS, asset, LLAssetType::AT_GESTURE, LLVFile::READ ) ;
			S32 size = file.getSize() ;

			char* buffer = new char[ size+1 ] ;
			if( NULL == buffer )
			{
				llerrs << "Memory Allocation Failed" << llendl;
				response->status( HTTP_INTERNAL_SERVER_ERROR ) ;
				return false ;
			}

			file.read((U8*)buffer, size);		/* Flawfinder: ignore */
			// ensure there's a trailing NULL so strlen will work.
			buffer[size] = '\0';

			LLMultiGesture* gesture = new LLMultiGesture();
			if( NULL == gesture )
			{
				llerrs << "Memory Allocation Failed" << llendl;
				response->status( HTTP_INTERNAL_SERVER_ERROR ) ;
				delete [] buffer ;
				return false ;
			}

			LLDataPackerAsciiBuffer dp(buffer, size+1);
			BOOL ok = gesture->deserialize(dp);
			if( ok )
			{
				info["Trigger"]       = gesture->mTrigger ;
				info["ReplaceText"]   = gesture->mReplaceText ;
			}
			else
			{
				response->status( HTTP_INTERNAL_SERVER_ERROR ) ;
			}

			delete [] buffer ;
			delete gesture ;
			return ok ;
		}
	};


	class NodeInventoryItem : public InterfaceNodeUUIDQuery
	{
	public:
		NodeInventoryItem( ) { }

		static void build( LLHTTPNode& node )
		{
			std::string path = "/Inventory/Item/<uuid>" ;
			node.addNode( path, new NodeInventoryItem() );
		}

		bool fetch(ResponsePtr response, LLSD& info,  LLUUID& id) const
		{
			LLViewerInventoryItem* item = gInventory.getItem( id );
			if( NULL == item /*|| ! item->isComplete()*/ )
			{
				response->notFound() ;
				return false ;
			}

			info = item->asLLSD() ;

			return true ;
		}
	};

	class NodeInventoryRoot : public InterfaceNode
	{
	public:
		NodeInventoryRoot( ) { }

		static void build( LLHTTPNode& node )
		{
			std::string path = "/Inventory/Root" ;
			node.addNode( path, new NodeInventoryRoot() );
		}

		virtual void get(ResponsePtr response, const LLSD& context) const
		{
			if( ! allowConnection( response, context ) )
				return ;

			LLSD info ;
			info["ID"] = gAgent.getInventoryRootID() ;
			response->result( info ) ;
		}
	};

	class NodeInventoryCategory : public InterfaceNodeUUIDQuery
	{
	public:
		NodeInventoryCategory( ) { }

		static void build( LLHTTPNode& node )
		{
			std::string path = "/Inventory/Category/<uuid>" ;
			node.addNode( path, new NodeInventoryCategory() );
		}

		bool fetch(ResponsePtr response, LLSD& info,  LLUUID& id) const
		{

			LLInventoryModel::cat_array_t    cats ;
			LLInventoryModel::item_array_t   items ;
			bool                             exclude_trash  = LLInventoryModel::EXCLUDE_TRASH ;
			gInventory.collectDescendents( id, cats, items, exclude_trash ) ;

			LLSD info_cats  = LLSD::emptyArray() ;
			for( S32 i = 0 ; i < cats.count() ; i++ )
				if( cats[i]->getParentUUID() == id )
					info_cats.append( cats[i]->getUUID() ) ;

			LLSD info_items = LLSD::emptyArray() ;
			for( S32 i = 0 ; i < items.count() ; i++ )
				if( items[i]->getParentUUID() == id )
					info_items.append( items[i]->getUUID() ) ;

			LLViewerInventoryCategory *cat = gInventory.getCategory( id ) ;
			info["Name"]        = cat->getName() ;
			info["Type"]        = LLAssetType::lookupHumanReadable( cat->getPreferredType() ) ;
			info["Categories"]  = info_cats ;
			info["Items"]       = info_items ;

			return true ;
		}
	};


	class NodeAssetNotecard : public InterfaceNodeUUIDQuery
	{
	public:
		NodeAssetNotecard( ) { }

		static void build( LLHTTPNode& node )
		{
			std::string path = "/Asset/Notecard/<uuid>" ;
			node.addNode( path, new NodeAssetNotecard() );
		}

		bool fetch(ResponsePtr response, LLSD& info,  LLUUID& id) const
		{
			if( NULL == gVFS )
			{
				llerrs << "gVFS is NULL" << llendl;
				response->status( HTTP_INTERNAL_SERVER_ERROR ) ;
				return false ;
			}

			if( ! gAssetStorage->hasLocalAsset( id, LLAssetType::AT_NOTECARD ) )
			{
				response->notFound() ;
				return false ;
			}
			// TODO:DZ return HTTP_ACCEPTED and queue getAssetData if not local

			LLVFile file( gVFS, id, LLAssetType::AT_NOTECARD, LLVFile::READ ) ;
			S32 size = file.getSize() ;

			char* buffer = new char[ size+1 ] ;
			if( NULL == buffer )
			{
				llerrs << "Memory Allocation Failed" << llendl;
				response->status( HTTP_INTERNAL_SERVER_ERROR ) ;
				return false ;
			}

			file.read((U8*)buffer, size);		/* Flawfinder: ignore */
			// ensure there's a trailing NULL so strlen will work.
			buffer[size] = '\0';

			LLNotecard nc ;
			bool ok ;
			{
				LLMemoryStream ms( (U8*) buffer, size ) ;
				ok = nc.importStream( ms ) ;
			}
			delete [] buffer ;

			if( ! ok )
			{
				llerrs << "Notecard import failed" << llendl;
				response->status( HTTP_INTERNAL_SERVER_ERROR ) ;
				return false ;
			}

			std::string::size_type i = 0 ;
			llwchar c = LLTextEditor::FIRST_EMBEDDED_CHAR ;
			LLWString wtext( utf8str_to_wstring( nc.getText() ) ) ;
			do
			{
				i = wtext.find( c, 0 ) ;
				if(  i != std::string::npos )
				{
					S32 index = c - LLTextEditor::FIRST_EMBEDDED_CHAR ;
					LLInventoryItem *inv = nc.getItems()[index] ;
					std::ostringstream oostr ;
					oostr <<  "<embed>" ;
					LLSDSerialize::toXML( inv->asLLSD(), oostr );
					oostr << "</embed>" ;
					wtext.replace( i, 1, utf8str_to_wstring( oostr.str() ) ) ;
					c++ ;
				}
			} while ( i != std::string::npos ) ;

			info["Version"]         = nc.getVersion() ;
			info["EmbeddedVersion"] = nc.getEmbeddedVersion() ;
			info["Text"]            = wstring_to_utf8str( wtext ) ;

			return true ;
		}
	};


	class NodeInterfaceConnect : public InterfaceNode
	{
	public:
		NodeInterfaceConnect( ) { }

		static void build( LLHTTPNode& node )
		{
			std::string path = "/Interface/Connect" ;
			node.addNode( path, new NodeInterfaceConnect() );
		}

		void post( ResponsePtr response, const LLSD& context, const LLSD& input ) const
		{
			LL_INFOS("NodeInterfaceConnect") << "context: " << context << LL_ENDL;
			LLHost remote = LLHost( context[CONTEXT_REQUEST]["remote-host"].asString(), context[CONTEXT_REQUEST]["remote-port"].asInteger() ) ;
			if( ! Interface::allowConnection( remote ))
			{
				response->notFound() ;
				return ;
			}

			if( Interface::channel.connected() )
			{
				LL_WARNS("NodeInterfaceConnect") << "interface connection attempt while already connected" << LL_ENDL;
				Interface::channel.reset() ;
				//response->methodNotAllowed() ;
				//return ;
			}
			if( ! input.has("port") )
			{
				LL_INFOS("NodeInterfaceConnect") << "interface port requested: " << LL_ENDL;
				LLSD info ;
				info["status"]  = "port requested" ;
				info["port"]    = gSavedSettings.getS32("InterfaceServerPort")+1 ;
				response->result( info ) ;
				return ;
			}

			Interface::session_id.setNull() ;
			Interface::channel.reset() ;
			Interface::channel.connect( input );
			if( ! Interface::channel.connected() )
			{
				LL_WARNS("NodeInterfaceConnect") << "interface unable to connect" << LL_ENDL;
				LLSD info ;
				info["status"]  = "unable to connect" ;
				response->result( info ) ;
				return ;
			}

			LL_INFOS("NodeInterfaceConnect") << "interface connection" << LL_ENDL;
			Interface::session_id.generate() ;
			LLSD info ;
			info["status"]  = "connected" ;
			info["session"] = Interface::session_id ;
			std::string cookie = "interfacesession=" + Interface::session_id.asString( ) + "; path=/";
			response->addHeader( "Set-Cookie", cookie ) ;
			response->result( info ) ;
			if( LLStartUp::getStartupState() == STATE_STARTED )
				Interface::statusLoginComplete() ;
		}
	};



}

namespace Snowglobe
{
	namespace Interface
	{
		void init(LLPumpIO* pump)
		{
/*			gSavedSettings.declareBOOL( "InterfaceAllowConnections",        true,  "Allow any connection to the Snowglobe API" ) ;
			gSavedSettings.declareBOOL( "InterfaceAllowRemoteConnections",  false, "Allow remote connections to the Snowglobe API" ) ;
			gSavedSettings.declareBOOL( "InterfaceVerifySessionCookie",     true,  "Verify session cookie as a credential measure to the Snowglobe API" ) ;
			gSavedSettings.declareS32(  "InterfaceServerPort",              50140, "Server port of the Snowglobe API" ) ;
*/
			if (LLAppViewer::instance()->getSecondInstance())
			{
				LL_INFOS("Interface::") << "Interface disabled for this instance" << LL_ENDL;
				return;
			}

			LL_INFOS("Interface::") << "InterfaceAllowConnections       : " << gSavedSettings.getBOOL("InterfaceAllowConnections") << LL_ENDL;
			LL_INFOS("Interface::") << "InterfaceAllowRemoteConnections : " << gSavedSettings.getBOOL("InterfaceAllowRemoteConnections") << LL_ENDL;
			LL_INFOS("Interface::") << "InterfaceVerifySessionCookie    : " << gSavedSettings.getBOOL("InterfaceVerifySessionCookie") << LL_ENDL;
			LL_INFOS("Interface::") << "InterfaceServerPort             : " << gSavedSettings.getS32("InterfaceServerPort") << LL_ENDL;
			Interface::pump = pump ;
			if( ! Interface::allowConnections() )
				return ;
			LLHTTPNode& r = LLIOHTTPServer::create( gAPRPoolp, *pump, gSavedSettings.getS32("InterfaceServerPort") ) ;
			NodeControlGroup::build( r ) ;
			NodeAgentGroups::build( r ) ;
			NodeAvatarTrackerFriends::build( r ) ;
			NodeAvatarTrackerFriend::build( r ) ;
			NodeInterfaceConnect::build( r ) ;
			NodeGestureManagerItems::build( r ) ;
			NodeGestureManagerItem::build( r ) ;
			NodeAssetNotecard::build( r ) ;
			NodeInventoryItem::build( r ) ;
			NodeInventoryCategory::build( r ) ;
			NodeInventoryRoot::build( r ) ;
		}
	}
}

