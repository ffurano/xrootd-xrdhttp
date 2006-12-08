/******************************************************************************/
/*                                                                            */
/*                          X r d C S 2 X m i . c c                           */
/*                                                                            */
/* (c) 2006 by the Board of Trustees of the Leland Stanford, Jr., University  */
/*                            All Rights Reserved                             */
/*   Produced by Andrew Hanushevsky for Stanford University under contract    */
/*              DE-AC02-76-SFO0515 with the Department of Energy              */
/******************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "Cns_api.h"

#include "XrdCS2/XrdCS2Req.hh"
#include "XrdCS2/XrdCS2Xmi.hh"

#include "Xrd/XrdScheduler.hh"
#include "XrdOuc/XrdOucError.hh"
#include "XrdOuc/XrdOucName2Name.hh"
#include "XrdOuc/XrdOucTimer.hh"
#include "XrdOuc/XrdOucPlatform.hh"
#define XRDOLBTRACETYPE ->
#include "XrdOlb/XrdOlbTrace.hh"
#include "XrdOlb/XrdOlbTypes.hh"
  
/******************************************************************************/
/*                         L o c a l   C l a s s e s                          */
/******************************************************************************/
  
namespace CS2Xmi
{
// This class is used to re-initialize the external initerface. It is always
// runasynchnrously by another thread is is scheduled whenever initialization
// is needed (e.g., after a communications failure).
//
class XmiInit : XrdJob
{
public:

void DoIt() {XmiP->InitXeq(); delete this;}

     XmiInit(XrdCS2Xmi *xp) : XrdJob("CS2 xmi init") {XmiP = xp;}
    ~XmiInit() {}

private:

XrdCS2Xmi *XmiP;
};

class XmiHelper
{
public:

const char *Path() {return xmiReq->Path();}

            XmiHelper(XrdOlbReq *reqP, const char *Path)
                        {xmiReq = XrdCS2Req::Alloc(reqP, Path);}
           ~XmiHelper() {xmiReq->Recycle();}
private:

XrdCS2Req *xmiReq;
};
}

/******************************************************************************/
/*                               G l o b a l s                                */
/******************************************************************************/

const char   *XrdCS2Xmi::prepTag  = "OlbXmiPrep";
const char   *XrdCS2Xmi::stageTag = "OlbXmiStage";

XrdOucError  *XrdCS2Xmi::eDest;
XrdInet      *XrdCS2Xmi::iNet;
XrdScheduler *XrdCS2Xmi::Sched ;
XrdOucTrace  *XrdCS2Xmi::Trace;

/******************************************************************************/
/*            E x t e r n a l   T h r e a d   I n t e r f a c e s             */
/******************************************************************************/
  
void *XrdCS2Xmi_StartPoll(void *parg)
{  
   XrdCS2Xmi *requestProcessor = (XrdCS2Xmi *)parg;

   requestProcessor->MSSPoll();

   return (void *)0;
}

/******************************************************************************/
/*                          X r d O l b g e t X m i                           */
/******************************************************************************/

// We allow the the following on the xmi directive line after the library path:

// [host[:port]] [-v version] [-s sclass]

// Where: host:port is the location of the Castor stager
//        version   is the version number (example, '-v3')
//        sclass    is the service class  (example, '-sfoo')

extern "C"
{
XrdOlbXmi *XrdOlbgetXmi(int argc, char **argv, XrdOlbXmiEnv *XmiEnv)
{

// Get an Xmi Object and return it
//
   return (XrdOlbXmi *)(new XrdCS2Xmi(XmiEnv));
}
}
  
/******************************************************************************/
/*                           C o n s t r u c t o r                            */
/******************************************************************************/
  
XrdCS2Xmi::XrdCS2Xmi(XrdOlbXmiEnv *Env)
{
   char *tp, *Parms, *cp;

// Set pointers and variables to zero
//
   prepReq      = 0;
   prepReqH     = 0;
   stageReq_ro  = 0;
   stageReq_rw  = 0;
   stageReq_ww  = 0;
   stageHlp_ro  = 0;
   stageHlp_rw  = 0;
   stageHlp_ww  = 0;
   initDone     = 0;
   initActive   = 0;

// Copy out needed stuff from the environment
//
   eDest = Env->eDest;         // -> Error message handler
   iNet  = Env->iNet;          // -> Network object
   Sched = Env->Sched;         // -> Thread scheduler
   Trace = Env->Trace;         // -> Trace handler

// Prefill the opts structure (this is always the same)
//
   Opts.stage_host    = NULL;
   Opts.service_class = NULL;
   Opts.stage_version = 2;
   Opts.stage_port    = 0;

// Obtain the optional data (we ignore malformed parameters)
//
   if (Env->Parms && *(Env->Parms))
      {Parms = strdup(Env->Parms); tp = Parms;
       while(*tp && *tp == ' ') tp++;
       if (*tp && *tp != '-')
          {Opts.stage_host = tp; tp = index(tp, ' ');
           if ((cp = index(Opts.stage_host, ':')))
              {*cp = '\0'; Opts.stage_port = atoi(cp+1);}
          }
       if (tp && &*tp)
          do {if (*tp) while(*tp && *tp == ' ') tp++;
              if (*tp != '-') break;
                   if (*(tp+1) == 'v') Opts.stage_version = atoi(tp+2);
              else if (*(tp+1) == 's') Opts.service_class = tp+2;
              else break;
              while(*tp && *tp != ' ') tp++;
              if (*tp) {*tp = '\0'; tp++;}
             } while(*tp);
      }

// Propogate the setting to the request object
//
   stage_apiInit(&Tinfo);
   setDefaultOption(&Opts);
   XrdCS2Req::Set(Env);

// Schedule initialization of this object
//
   Init(0,1);
};

/******************************************************************************/
/*                                  I n i t                                   */
/******************************************************************************/
  
void XrdCS2Xmi::Init(int deltatime, int force)
{
  EPNAME("CS2Init");
  XrdJob *jp = (XrdJob *)new CS2Xmi::XmiInit(this);

// Turn off the initdone flag unless we are already running
//
   initMutex.Lock();
   if (force) initActive = 0;
      else if (initActive)
              {DEBUG("CS2 Init not scheduled; already started.");
               initMutex.UnLock();
               return;
              }
   initDone = 0;
   initMutex.UnLock();

// Schedule this initialization job as needed
//
   if (deltatime) Sched->Schedule(jp, time(0)+deltatime);
      else        Sched->Schedule(jp);
}
  
/******************************************************************************/
/*                               I n i t X e q                                */
/******************************************************************************/
  
void XrdCS2Xmi::InitXeq()
{
   EPNAME("InitXeq");
   static int PollerOK = 0;
   pthread_t tid;
   int       retc, TimeOut;
   mode_t    mask;

// First we must verify that we haven't been initialized yet. There may be
// several threads that have noticed that re-initialization is needed.
//
   initMutex.Lock();
   if (initDone)
      {DEBUG("CS2 Init skipped; already initialized.");
       initMutex.UnLock(); 
       return;
      }

// Obtain our umask (sort of silly if you ask me)
//
   mask = umask(0);
   umask(mask);

// Delete any objects we may have allocated here and try again
//
   if (prepReq)      {delete prepReq;      prepReq      = 0;}
   if (prepReqH)     {delete prepReqH;     prepReqH     = 0;}
   if (stageReq_ro)  {delete stageReq_ro;  stageReq_ro  = 0;}
   if (stageReq_rw)  {delete stageReq_rw;  stageReq_rw  = 0;}
   if (stageReq_ww)  {delete stageReq_ww;  stageReq_ww  = 0;}
   if (stageHlp_ro)  {delete stageHlp_ro;  stageHlp_ro  = 0;}
   if (stageHlp_rw)  {delete stageHlp_rw;  stageHlp_rw  = 0;}
   if (stageHlp_ww)  {delete stageHlp_ww;  stageHlp_ww  = 0;}

// First off, tell the base object to stop logging
//
try {
   castor::BaseObject::initLog("", castor::SVC_NOMSG);
   TimeOut = stage_getClientTimeout();
   DEBUG("Castor timeout = " <<TimeOut);

// Now create the objects used for prepare processing. We want a separate set
// of objects here because we will not be issuing queries for these requests
//
  prepClient = new castor::client::BaseClient(TimeOut);
  prepClient->setOption(&Opts);

  prepReq    = new castor::stager::StagePrepareToGetRequest;
  prepReq->setMask(mask);
  prepReq->setUserTag(prepTag);

  prepReqH   = new castor::stager::RequestHelper(prepReq);
  prepReqH->setOptions(&Opts);

// Now create the objects used for stage processing. We want a separate set
// of objects here because we *will* be issuing queries for these requests
//
  stageClient = new castor::client::BaseClient(TimeOut);
  stageClient->setOption(&Opts);

  stageReq_ro = new castor::stager::StagePrepareToGetRequest;
  stageReq_ro->setUserTag(stageTag);

  stageHlp_ro = new castor::stager::RequestHelper(stageReq_ro);
  stageHlp_ro->setOptions(&Opts);

  stageReq_rw = new castor::stager::StagePrepareToUpdateRequest;
  stageReq_rw->setUserTag(stageTag);
  stageReq_rw->setMask(mask);

  stageHlp_rw = new castor::stager::RequestHelper(stageReq_rw);
  stageHlp_rw->setOptions(&Opts);

  stageReq_ww = new castor::stager::StagePrepareToPutRequest;
  stageReq_ww->setUserTag(stageTag);
  stageReq_ww->setMask(mask);

  stageHlp_ww = new castor::stager::RequestHelper(stageReq_ww);
  stageHlp_ww->setOptions(&Opts);
  }

// We catch some common problems here. Note that a communications error
// will cause initialization to be retried later. In the mean time, requests
// will be told to wait until initialization completes
//
  catch (castor::exception::Communication e)
        {eDest->Emsg("cs2", "Communications error;", e.getMessage().str().c_str());
         Init(reinitTime, 1);
         initMutex.UnLock();
         return;
        }

// For real exceptions we have no choice but to terminate ourselves
//
  catch (castor::exception::Exception e)
        {eDest->Emsg("cs2","Fatal exception;",e.getMessage().str().c_str());
         _exit(128);
        }

// Start the thread that handles polling
//
   if (!PollerOK)
      if ((retc = XrdOucThread::Run(&tid, XrdCS2Xmi_StartPoll, (void *)this,
                                     XRDOUCTHREAD_BIND, "cs2 request polling")))
         eDest->Emsg("CS2Xmi", retc, "create polling thread");
         else PollerOK = 1;

// All done
//
   initDone   = 1;
   initActive = 0;
   initMutex.UnLock();
   eDest->Say("XrdCS2Xmi: CS2 interface initialized.");
}
 
/******************************************************************************/
/*                               M S S P o l l                                */
/******************************************************************************/
  
void XrdCS2Xmi::MSSPoll()
{
   EPNAME("MSSPoll");
   struct stage_query_req       *requests;  // <- this is allocated by next
   struct stage_filequery_resp  *resp;
   XrdCS2Req                    *reqP;
   char                          errbuf[1024];
   int                           nbresps, i, pcnt;

// Set the location of the error buffer
//
   stage_seterrbuf(errbuf, sizeof(errbuf));

// Create the query type here. We would prefer to use the internal API as the
// external one copies large amounts of data that we just don't need, sigh.
//
   create_query_req(&requests, 1);
   requests[0].type  = BY_USERTAG_GETNEXT;
   requests[0].param = (void *)stageTag;

// Endless loop waiting for thing to query. Eventually, we will have to be
// responsive to file creation requests (though who knows how).
//
do{pcnt = XrdCS2Req::Wait4Q();
   DEBUG("polling for " <<pcnt <<" request(s)");
   while(pcnt > 0)
        {*errbuf = '\0';
         if (stage_filequery(requests, 1, &resp, &nbresps, &Opts) < 0)
            {if (serrno != 0)
                eDest->Emsg("MSSPoll","stage_filequery() failed;",sstrerror(serrno));
             if (*errbuf)
                eDest->Emsg("MSSPoll", errbuf);
             Init(0);
             continue;
            }

          DEBUG("Received " <<nbresps <<" stage_filequery() responses");

          for (i = 0; i < nbresps; i++)
              {DEBUG(stage_fileStatusName(resp[i].status) <<" rc=" <<
                     resp[i].errorCode <<" path=" <<resp[i].diskserver <<':'
                     <<resp[i].filename <<" (" <<resp[i].castorfilename <<')');
               if (*resp[i].castorfilename)
                  {pcnt--;
                   if ((reqP = XrdCS2Req::Remove(resp[i].castorfilename)))
                      if (!resp[i].errorCode) sendRedirect(reqP, &resp[i]);
                         else sendError(reqP, resp[i].castorfilename,
                                        resp[i].errorCode, resp[i].errorMessage);
                  }
              }
          free_filequery_resp(resp, nbresps);
          if (pcnt > 0) XrdOucTimer::Wait(MSSPollTime*1000);
         }
   } while(1);
}

/******************************************************************************/
/*                    F u n c t i o n a l   M e t h o d s                     */
/******************************************************************************/
/******************************************************************************/
/*                                 C h m o d                                  */
/******************************************************************************/
  
int XrdCS2Xmi::Chmod(XrdOlbReq *Request, const char *path, mode_t mode)
{
   CS2Xmi::XmiHelper myRequest(Request, path);

// Try to change the mode
//
   if (Cns_chmod(myRequest.Path(), mode))
      return sendError(Request, serrno, "chmod", path);

// Tell client all went well
//
   Request->Reply_OK();
   return 1;
}
  
/******************************************************************************/
/*                                 M k d i r                                  */
/******************************************************************************/
  
int XrdCS2Xmi::Mkdir(XrdOlbReq *Request, const char *path, mode_t mode)
{
   CS2Xmi::XmiHelper myRequest(Request, path);

// Try to change the mode
//
   if (Cns_mkdir(myRequest.Path(), mode))
      return sendError(Request, serrno, "mkdir", path);

// Tell client all went well
//
   Request->Reply_OK();
   return 1;
}
  
/******************************************************************************/
/*                                M k p a t h                                 */
/******************************************************************************/
  
int XrdCS2Xmi::Mkpath(XrdOlbReq *Request, const char *path, mode_t mode)
{
   CS2Xmi::XmiHelper myRequest(Request, path);
   char *next_path, *this_path = (char *)myRequest.Path();
   struct Cns_filestat cnsbuf;
   int retc;

// Trim off the trailing slash so that we make everything but the last component
//
   if (!(retc = strlen(this_path))) 
      return sendError(Request, ENOENT, "mkpath", path);
   while(retc && this_path[retc-1] == '/') 
        {retc--; this_path[retc] = '\0';}

// Typically, the path exists. So, do a quick check before launching into it
//
   if (!(next_path = rindex(this_path, (int)'/'))
   ||  next_path == this_path) {Request->Reply_OK(); return 1;}
   *next_path = '\0';
   if (!Cns_stat(this_path, &cnsbuf)) {Request->Reply_OK(); return 1;}
   *next_path = '/';

// Start creating directories starting with the root
//
   next_path = this_path;
   while((next_path = index(next_path+1, int('/'))))
        {*next_path = '\0';
         if (Cns_mkdir(this_path, mode) && serrno != EEXIST) 
            return sendError(Request, serrno, "mkdir", path);
         *next_path = '/';
        }

// All done
//
   Request->Reply_OK();
   return 1;
}
  
/******************************************************************************/
/*                                R e n a m e                                 */
/******************************************************************************/
  
int XrdCS2Xmi::Rename(XrdOlbReq *Request, const char *opath,
                                          const char *npath)
{
   CS2Xmi::XmiHelper myRequestOld(Request, opath);
   CS2Xmi::XmiHelper myRequestNew(Request, npath);

// Try to change the mode
//
   if (Cns_rename(myRequestOld.Path(), myRequestNew.Path()))
      return sendError(Request, serrno, "rename", opath);

// Tell client all went well
//
   Request->Reply_OK();
   return 1;
}
  
/******************************************************************************/
/*                                R e m d i r                                 */
/******************************************************************************/
  
int XrdCS2Xmi::Remdir(XrdOlbReq *Request, const char *path)
{
   CS2Xmi::XmiHelper myRequest(Request, path);

// Try to change the mode
//
   if (Cns_rmdir(myRequest.Path()))
      return sendError(Request, serrno, "rmdir", path);

// Tell client all went well
//
   Request->Reply_OK();
   return 1;
}
  
/******************************************************************************/
/*                                R e m o v e                                 */
/******************************************************************************/
  
int XrdCS2Xmi::Remove(XrdOlbReq *Request, const char *path)
{
   CS2Xmi::XmiHelper myRequest(Request, path);

// Try to change the mode
//
   if (Cns_delete(myRequest.Path()))
      return sendError(Request, serrno, "rm", path);

// Tell client all went well
//
   Request->Reply_OK();
   return 1;
}
  
/******************************************************************************/
/*                                S e l e c t                                 */
/******************************************************************************/

int XrdCS2Xmi::Select( XrdOlbReq *Request, const char *path, int opts)
{
   EPNAME("Select");
   castor::stager::Request *req;
   castor::stager::SubRequest *subreq = new castor::stager::SubRequest();
   std::vector<castor::rh::Response *>respvec;
   castor::client::VectorResponseHandler rh(&respvec);
   XrdCS2Req *myRequest;
   int dopoll = 0;
   const char *rType;

// Perform an initialization Check
//
   if (!initDone) {Request->Reply_Wait(retryTime); return 1;}
   stage_apiInit(&Tinfo);

// Allocate a request object for this request
//
   if (!(myRequest = XrdCS2Req::Alloc(Request, path))) return 1;

// Create a new Castor request
//
    subreq->setProtocol(std::string("xroot"));
    subreq->setFileName(std::string(myRequest->Path()));
    subreq->setModeBits(0744);

// There are three possibilities here:r/o files, r/w files, new files
// We need to separate each type and issue the request appropriately
// Too bad the class design is not orthogonal. It would have been easier.
//
        if (opts & XMI_NEW) {stageReq_ww->addSubRequests(subreq);
                             subreq->setRequest(stageReq_ww);
                             req = stageReq_ww;
                             rType = "Prep2Put";
                            }
   else if (opts & XMI_RW)  {stageReq_rw->addSubRequests(subreq);
                             subreq->setRequest(stageReq_rw);
                             req = stageReq_rw;
                             rType = "Prep2Upd"; dopoll = 1;
                            }
   else                     {stageReq_ro->addSubRequests(subreq);
                             subreq->setRequest(stageReq_ro);
                             req = stageReq_ro;
                             rType = "Prep2Get"; dopoll = 1;
                            }

// Now send the request (we have no idea what to do with reqid which will
// generate a warning, sigh). We also need to lock the response queue
// for this request prior to adding it to the queue of pending responses.
// Since Recycle() automatically unlocks the queue so we don't explictly do it.
//
   myRequest->Lock();
   try   {std::string reqid = stageClient->sendRequest(req, &rh);
          DEBUG(rType << " reqid=" <<reqid.c_str() <<" path=" <<path);
         }
   catch (castor::exception::Communication e)
         {myRequest->Recycle();
          eDest->Emsg("Select","Communications error;",e.getMessage().str().c_str());
          Init(reinitTime);
          Request->Reply_Wait(retryTime);
          return 1;
         }
   catch (castor::exception::Exception e)
         {myRequest->Recycle();
          eDest->Emsg("Select","sendRequest exception;",e.getMessage().str().c_str());
          Request->Reply_Error("Unexpected communications exception.");
          return 1;
         }

// Make sure we have an initial response.
//
   if (respvec.size() <= 0)
      {myRequest->Recycle();
       eDest->Emsg("Select", "No response for select", path);
       Request->Reply_Error("Internal Castor2 error receiving response.");
       return 1;
      }

// Proccess the response.
//
   castor::rh::IOResponse* fr =
               dynamic_cast<castor::rh::IOResponse*>(respvec[0]);
   if (0 == fr)
      {myRequest->Recycle();
       eDest->Emsg("Select", "Invalid reesponse object for select", path);
       Request->Reply_Error("Internal Castor2 error casting response.");
       return 1;
      }

// Send an error or just add this to the queue (sendError/Recycle does unlock)
//
   if (fr->errorCode())
      sendError(myRequest,path,fr->errorCode(),fr->errorMessage().c_str());
      else myRequest->Queue();

// Free response data and return
//
   delete respvec[0];
   return 1;
}

/******************************************************************************/
/*                                  S t a t                                   */
/******************************************************************************/

int XrdCS2Xmi::Stat(XrdOlbReq *Request, const char *path)
{
   CS2Xmi::XmiHelper myRequest(Request, path);
   struct Cns_filestat cnsbuf;
   struct stat statbuf;

// Get information about the file
//
   if (Cns_stat(myRequest.Path(), &cnsbuf))
      return sendError(Request, serrno, "stat", path);

// Convert the Cns stat buffer to a normal Unix one
//
   memset(&statbuf, 0, sizeof(statbuf));
   statbuf.st_mode  = cnsbuf.filemode;
   statbuf.st_nlink = cnsbuf.nlink;
   statbuf.st_uid   = cnsbuf.uid;
   statbuf.st_gid   = cnsbuf.gid;
   statbuf.st_size  = static_cast<off_t>(cnsbuf.filesize);
   statbuf.st_atime = cnsbuf.atime;
   statbuf.st_ctime = cnsbuf.ctime;
   statbuf.st_mtime = cnsbuf.mtime;
   if (cnsbuf.status == '-') statbuf.st_ino = 1;

// Now send the response (let the request object figure out the format)
//
   Request->Reply_OK(statbuf);
   return 1;
}
  
/******************************************************************************/
/*                       P r i v a t e   M e t h o d s                        */
/******************************************************************************/
/******************************************************************************/
/*                             s e n d E r r o r                              */
/******************************************************************************/
  
int XrdCS2Xmi::sendError(XrdOlbReq *reqP, int rc, const char *opn, 
                                                  const char *path)
{
   EPNAME("sendError");
   static int Retries = 255;
   const char *ecode;
   char buff[256];

// Convert errnumber to err string
//
   switch(rc)
         {case ENOENT:        ecode = "ENOENT";      break;
          case EPERM:         ecode = "EISDIR";      break;// Wierd but documented
          case EEXIST:        ecode = "ENOTEMPTY";   break;// Wierd but documented
          case EACCES:        ecode = "EACCES";      break;
          case ENOSPC:        ecode = "ENOSPC";      break;
          case EFAULT:        ecode = "ENOMEM";      break;// No other choice here
          case ENOTDIR:       ecode = "ENOTDIR";     break;
          case ENAMETOOLONG:  ecode = "ENAMETOOLONG";break;
          case SENOSHOST:     ecode = "ENETUNREACH"; break;
          case SENOSSERV:     ecode = "ENETUNREACH"; break;
          case SECOMERR:      ecode = 0;             break;// Comm error
          case ENSNACT:       ecode = 0;             break;// NS is down
          default:            ecode = "EINVAL";
                              break;
         }

// If we have an error tag then we can send it
//
   if (ecode)
      {snprintf(buff, sizeof(buff), "%s failed; %s", opn, sstrerror(rc));
       buff[sizeof(buff)-1] = '\0';
       reqP->Reply_Error(ecode, buff);
       DEBUG("msg='" <<buff <<"' path=" <<path);
       return 1;
      }

// For communication errors we will tell the client to try again later
//
   Retries++;
   if (!(Retries & 255)) eDest->Emsg("cs2", opn, "failed;", sstrerror(rc));
   reqP->Reply_Wait(retryTime);
   return 1;
}

/******************************************************************************/
int XrdCS2Xmi::sendError(XrdCS2Req *reqP, const char *fn, int rc, const char *emsg)
{
   EPNAME("sendError");
   char buff[1024];

   snprintf(buff, sizeof(buff), "Staging error %d: %s (%s)", rc, 
                                 sstrerror(rc), (emsg ? emsg : ""));
   buff[sizeof(buff)-1] = '\0';
   DEBUG("msg='" <<buff <<"'; path=" <<fn);

   do {reqP->Request()->Reply_Error(buff);} while((reqP = reqP->Recycle()));
   return 1;
}

/******************************************************************************/
/*                          s e n d R e d i r e c t                           */
/******************************************************************************/
  
void XrdCS2Xmi::sendRedirect(XrdCS2Req                   *reqP,
                             struct stage_filequery_resp *resp)
{
   EPNAME("sendRedirect");
   char buff[256];

// Send the redirect
//
   do{sprintf(buff, "&cs2.fid=%s", resp->filename);
      DEBUG("-> " <<resp->diskserver <<'?' <<buff <<" path=" <<reqP->Path());
      reqP->Request()->Reply_Redirect(resp->diskserver, 0, buff);
     } while((reqP = reqP->Recycle()));
}

/******************************************************************************/
/*                                                                            */
/*                          n o t S u p p o r t e d                           */
/*                                                                            */
/******************************************************************************/
  
int XrdCS2Xmi::notSupported(XrdOlbReq *rp, const char *opn, const char *path)
{
   char buff[2048];
   int n;

   n = sprintf(buff, "Unable to %s %s; operation not supported.",opn,path);
   rp->Reply_Error(buff, n);
   return 1;
}