// Session

// Session stores/maintains information about an authenticated logical connection
// between servers/clients.  Sessions are only apply to authenticated connections;
// do not create sessions for unauthenticated connections within a client/server.

// There are two types of sessions: non-remote and remote.  Non-remote sessions are
// generated by the server and are unique within the server.  Remote sessions are
// generated by a remote source and may or may not be unique within the server.  Note
// that the uniqueness of a Session is determined only by its id.

// Sessions contain a unique id (ushort), encryption attributes, a time stamp for
// the last use of the session, symmetric key for encryption/decryption (optional),
// AuthCertificate used to created the session (optional), and sequence numbers for
// encryption/decryption (optional).

// Session instances are created by authentication protocols, maintained by the
// SessionMgr, and used by encryption protocols.

// NOTE: Only the Id, isRemote, EncryptAttributes, SymmetricKey, and AuthCertificate 
// are not mutable within a Session.  Is expected and desired that other members be
// updated on const instances.


#include "common/won.h"
#include <time.h>
#include "common/CriticalSection.h"
#include "crypt/SymmetricKey.h"
#include "AuthCertificateBase.h"
#include "Session.h"

// Private namespace for using and constants
namespace {
	using WONCommon::AutoCrit;
	using WONCrypt::SymmetricKey;
	using WONAuth::AuthCertificateBase;
	using WONAuth::EncryptAttributes;
	using WONAuth::Session;
	using WONAuth::SessionData;
};


// Class Members
unsigned short Session::gNextSessionId = 1;  // Next session id to use (disallow 0)
const Session  Session::gInvalidSession(0);  // Invalid Session object


// ** SessionData - Constructors / Destructor **

// Default constructor
SessionData::SessionData() :
	mRefCt(1),
	mId(0),
	mIsRemote(false),
	mEncryptAttr(),
	mKeyP(NULL),
	mCertP(NULL),
	mLastAction(0),
	mRecvSeq(0),
	mSendSeq(0),
	mRecvCt(0),
	mSendCt(0),
	mProcCt(0),
	mCrit()
{}


// Destructor
SessionData::~SessionData()
{
	delete mKeyP;
	delete mCertP;
}


// ** Constructors / Destructor

// Default constructor - creates a Invalid Session instance
Session::Session() :
	mRefP(gInvalidSession.mRefP)
{
	InterlockedIncrement(&(gInvalidSession.mRefP->mRefCt));
}


// Construct and initialize
Session::Session(const EncryptAttributes& theAttr, SymmetricKey* theKeyP,
                 AuthCertificateBase* theCertP, unsigned short theId) :
	mRefP(new SessionData)
{
	Init(theAttr, theKeyP, theCertP, theId);
}


// Private Constructor - Construct with existing session id for searching
Session::Session(unsigned short theId) :
	mRefP(new SessionData)
{
	mRefP->mId = theId;
}


// Copy Constructor
Session::Session(const Session& theSessR) :
	mRefP(theSessR.mRefP)
{
	InterlockedIncrement(&(theSessR.mRefP->mRefCt));
}

	
// Destructor
Session::~Session()
{
	if (InterlockedDecrement(&(mRefP->mRefCt)) <= 0)
		delete mRefP;
}


// ** Private Methods **

unsigned short
Session::GetNextSessionId()
{
	unsigned short aRet = gNextSessionId++;
	if (gNextSessionId == 0) gNextSessionId = 1;  // 0 not a valid session id
	return aRet;
}


// ** Public Methods **

// Assignment operator
Session&
Session::operator=(const Session& theSessR)
{
	if (mRefP != theSessR.mRefP)
	{
		InterlockedIncrement(&(theSessR.mRefP->mRefCt));
		if (InterlockedDecrement(&(mRefP->mRefCt)) <= 0)
			delete mRefP;

		mRefP = theSessR.mRefP;
	}
	return *this;
}


bool
Session::IsValid() const
{
	if (mRefP->mId == 0) return false;

	return (mRefP->mEncryptAttr.mEncryptMode != ENCRYPT_NONE ?
	        ((mRefP->mKeyP) && (mRefP->mCertP)) : true);
}


void
Session::Init(const EncryptAttributes& theAttr, SymmetricKey* theKeyP,
              AuthCertificateBase* theCertP, unsigned short theId)
{
	// If current session is shared, break reference link and init a new session
	// NOTE: Must still check for a 0 ref count in case a thread switch happened
	// between if and decrement.
	if (mRefP->mRefCt > 1)
	{
		if (InterlockedDecrement(&(mRefP->mRefCt)) <= 0)
			delete mRefP;
		mRefP = new SessionData;
	}

	// Init session as remote session (theId specified) or new session
	AutoCrit aCrit(mRefP->mCrit);  // Lock
	if (theId > 0)
	{
		mRefP->mId       = theId;
		mRefP->mIsRemote = true;
	}
	else
	{
		mRefP->mId       = GetNextSessionId();
		mRefP->mIsRemote = false;
	}

	// Init session attributes
	mRefP->mEncryptAttr = theAttr;
	mRefP->mKeyP        = theKeyP;
	mRefP->mCertP       = theCertP;

	// Init seq numbers and update lastAction
	mRefP->mRecvSeq = mRefP->mSendSeq = 0;
	Touch();
}


bool
Session::TestSetRecvSeq(unsigned short theSeq, bool isReliable) const
{
	AutoCrit aCrit(mRefP->mCrit);  // Lock
	bool aRet = (isReliable ? (mRefP->mRecvSeq + 1) == theSeq
	                        : mRefP->mRecvSeq < theSeq);

	if (aRet) mRefP->mRecvSeq = theSeq;
	return aRet;
}


bool
Session::TestSetSendSeq(unsigned short theSeq, bool isReliable) const
{
	AutoCrit aCrit(mRefP->mCrit);  // Lock
	bool aRet = (isReliable ? (mRefP->mSendSeq + 1) == theSeq
	                        : mRefP->mSendSeq < theSeq);

	if (aRet) mRefP->mSendSeq = theSeq;
	return aRet;
}


// Session::Dump
// Streaming method.  Outputs session info.
void
Session::Dump(std::ostream& os, bool verbose) const
{
	if (! verbose)
	{
		char* aTStr = ctime(&(mRefP->mLastAction));
		*(aTStr + strlen(aTStr) - 1) = '\0';  // Clobber newline
		os << "(Session: " << mRefP->mId << ' ' << aTStr << ')';
	}
	else
	{
		os << "Session (Id=" << mRefP->mId << ')' << endl;
		os << "  LastAction  = " << ctime(&(mRefP->mLastAction));
		os << "  EncryptAttr = " << mRefP->mEncryptAttr << endl;
		os << "  RecvSeq     = " << mRefP->mRecvSeq     << endl;
		os << "  SendSeq     = " << mRefP->mSendSeq     << endl;

		os << "  SymmetricKey: ";
		if (mRefP->mKeyP) os << *(mRefP->mKeyP);
		else       os << "NULL";

		os << "Certificate: ";
		if (mRefP->mCertP) os << *(mRefP->mKeyP);
		else        os << "NULL";
	}
}


// ** Output Operator **

std::ostream&
operator<<(std::ostream& os, const EncryptAttributes& theAttrR)
{
	os << '(';
	switch (theAttrR.mEncryptMode)
	{
	case WONAuth::ENCRYPT_NONE:
		os << "NONE";      break;
	case WONAuth::ENCRYPT_BLOWFISH:
		os << "BLOWFISH";  break;
	default:
		os << "UNKNOWN";   break;
	}

	os << ' ' << theAttrR.mIsSequenced << ' ' << theAttrR.mIsSession << ' '
	   << theAttrR.mEncryptAll << ')';

	return os;
}