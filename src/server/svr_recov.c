/*
*         OpenPBS (Portable Batch System) v2.3 Software License
*
* Copyright (c) 1999-2000 Veridian Information Solutions, Inc.
* All rights reserved.
*
* ---------------------------------------------------------------------------
* For a license to use or redistribute the OpenPBS software under conditions
* other than those described below, or to purchase support for this software,
* please contact Veridian Systems, PBS Products Department ("Licensor") at:
*
*    www.OpenPBS.org  +1 650 967-4675                  sales@OpenPBS.org
*                        877 902-4PBS (US toll-free)
* ---------------------------------------------------------------------------
*
* This license covers use of the OpenPBS v2.3 software (the "Software") at
* your site or location, and, for certain users, redistribution of the
* Software to other sites and locations.  Use and redistribution of
* OpenPBS v2.3 in source and binary forms, with or without modification,
* are permitted provided that all of the following conditions are met.
* After December 31, 2001, only conditions 3-6 must be met:
*
* 1. Commercial and/or non-commercial use of the Software is permitted
*    provided a current software registration is on file at www.OpenPBS.org.
*    If use of this software contributes to a publication, product, or
*    service, proper attribution must be given; see www.OpenPBS.org/credit.html
*
* 2. Redistribution in any form is only permitted for non-commercial,
*    non-profit purposes.  There can be no charge for the Software or any
*    software incorporating the Software.  Further, there can be no
*    expectation of revenue generated as a consequence of redistributing
*    the Software.
*
* 3. Any Redistribution of source code must retain the above copyright notice
*    and the acknowledgment contained in paragraph 6, this list of conditions
*    and the disclaimer contained in paragraph 7.
*
* 4. Any Redistribution in binary form must reproduce the above copyright
*    notice and the acknowledgment contained in paragraph 6, this list of
*    conditions and the disclaimer contained in paragraph 7 in the
*    documentation and/or other materials provided with the distribution.
*
* 5. Redistributions in any form must be accompanied by information on how to
*    obtain complete source code for the OpenPBS software and any
*    modifications and/or additions to the OpenPBS software.  The source code
*    must either be included in the distribution or be available for no more
*    than the cost of distribution plus a nominal fee, and all modifications
*    and additions to the Software must be freely redistributable by any party
*    (including Licensor) without restriction.
*
* 6. All advertising materials mentioning features or use of the Software must
*    display the following acknowledgment:
*
*     "This product includes software developed by NASA Ames Research Center,
*     Lawrence Livermore National Laboratory, and Veridian Information
*     Solutions, Inc.
*     Visit www.OpenPBS.org for OpenPBS software support,
*     products, and information."
*
* 7. DISCLAIMER OF WARRANTY
*
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT
* ARE EXPRESSLY DISCLAIMED.
*
* IN NO EVENT SHALL VERIDIAN CORPORATION, ITS AFFILIATED COMPANIES, OR THE
* U.S. GOVERNMENT OR ANY OF ITS AGENCIES BE LIABLE FOR ANY DIRECT OR INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* This license will be governed by the laws of the Commonwealth of Virginia,
* without reference to its choice of law rules.
*/

/**
 * @file svr_recov.c
 *
 * contains functions to save server state and recover
 *
 * Included functions are:
 * svr_recov()
 * svr_save()
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/param.h>
#include "pbs_ifl.h"
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "queue.h"
#include "server.h"
#include "svrfunc.h"
#include "log.h"
#include "../lib/Liblog/pbs_log.h"
#include "../lib/Liblog/log_event.h"
#include "lib_ifl.h"
#include "pbs_error.h"
#include "resource.h"
#include "utils.h"
#include <string>
#include <sstream> 
#include "xml_recov.h"/* save_attr_xml & libxml/tree.h */

#ifndef MAXLINE
#define MAXLINE 1024
#endif
/* Global Data Items: */

extern struct server server;
extern attribute_def svr_attr_def[];
extern char     *path_svrdb;
extern char     *path_svrdb_new;
extern char     *path_priv;
extern char     *msg_svdbopen;
extern char     *msg_svdbnosv;
extern time_t    pbs_incoming_tcp_timeout;



/**
 * Recover server state from server database.
 *
 * @param ps     A pointer to the server state structure.
 * @param mode   This is either SVR_SAVE_QUICK or SVR_SAVE_FULL.
 * @return       Zero on success or -1 on error.
 */

int svr_recov(

  char *svrfile,  /* I */
  int read_only)  /* I */

  {
  int  i;
  int  sdb;
  char log_buf[LOCAL_LOG_BUF_SIZE];

  void recov_acl(pbs_attribute *, attribute_def *, const char *, const char *);

  sdb = open(svrfile, O_RDONLY, 0);

  if (sdb < 0)
    {
    if (errno == ENOENT)
      {
      char tmpLine[LOG_BUF_SIZE];

      snprintf(tmpLine, sizeof(tmpLine), "cannot locate server database '%s' - use 'pbs_server -t create' to create new database if database has not been initialized.",
               svrfile);

      log_err(errno, __func__, tmpLine);
      }
    else
      {
      log_err(errno, __func__, msg_svdbopen);
      }

    return(-1);
    }

  /* read in server structure */
  lock_sv_qs_mutex(server.sv_qs_mutex, __func__);

  i = read_ac_socket(sdb, (char *) & server.sv_qs, sizeof(server_qs));

  if (i != sizeof(server_qs))
    {
    unlock_sv_qs_mutex(server.sv_qs_mutex, log_buf);

    if (i < 0)
      log_err(errno, __func__, "read of serverdb failed");
    else
      log_err(errno, __func__, "short read of serverdb");

    close(sdb);

    return(-1);
    }

  /* Save the sv_jobidnumber field in case it is set by the attributes. */
  i = server.sv_qs.sv_jobidnumber;

  /* read in server attributes */

  if (recov_attr(
        sdb,
        &server,
        svr_attr_def,
        server.sv_attr,
        SRV_ATR_LAST,
        0,
        !read_only) != 0 ) 
    {
    unlock_sv_qs_mutex(server.sv_qs_mutex, log_buf);
    log_err(errno, __func__, "error on recovering server attr");

    close(sdb);

    return(-1);
    }

  /* Restore the current job number and make it visible in qmgr print server commnad. */

  if (!read_only)
    {
    server.sv_qs.sv_jobidnumber = i;

    server.sv_attr[SRV_ATR_NextJobNumber].at_val.at_long = i;

    server.sv_attr[SRV_ATR_NextJobNumber].at_flags |= ATR_VFLAG_SET| ATR_VFLAG_MODIFY;
    }

  unlock_sv_qs_mutex(server.sv_qs_mutex, __func__);

  close(sdb);

  /* recover the server various acls from their own files */

  for (i = 0;i < SRV_ATR_LAST;i++)
    {
    if (server.sv_attr[i].at_type == ATR_TYPE_ACL)
      {
      recov_acl(
        &server.sv_attr[i],
        &svr_attr_def[i],
        PBS_SVRACL,
        svr_attr_def[i].at_name);

      if ((!read_only) && (svr_attr_def[i].at_action != (int (*)(pbs_attribute*, void*, int))0))
        {
        svr_attr_def[i].at_action(
          &server.sv_attr[i],
          &server,
          ATR_ACTION_RECOV);
        }
      }
    }    /* END for (i) */

  return(PBSE_NONE);
  }  /* END svr_recov() */



int svr_recov_xml(

  char *svrfile,  /* I */
  int   read_only)  /* I */

  {
  int   errorCount = 0;
  int   rc;

  char  buffer[MAXLINE<<10];
  char  log_buf[LOCAL_LOG_BUF_SIZE];
  
  memset(&buffer, 0, sizeof(buffer));

  xmlDocPtr doc = xmlReadFile(svrfile,NULL,0);
  if (doc == NULL)
    {
    if (errno == ENOENT)
      {
      snprintf(log_buf,sizeof(log_buf),
        "cannot locate server database '%s' - use 'pbs_server -t create' to create new database if database has not been initialized.",
        svrfile);

      log_err(errno, __func__, log_buf);
      }
    else
      {
      log_err(errno, __func__, msg_svdbopen);
      }
    xmlFreeDoc(doc);
    return(-1);
    }

  xmlNodePtr root_node = xmlDocGetRootElement(doc);
  std::string svrdb_node((char*)root_node->name);
  if (svrdb_node != "server_db")
    {
    /* no server tag - check if this is the old format */
    log_event(PBSEVENT_SYSTEM,
      PBS_EVENTCLASS_SERVER,
      __func__,
      "Cannot find a server tag, attempting to load legacy format\n");

    xmlFreeDoc(doc);
    rc = svr_recov(svrfile,read_only);

    return(rc);
    }

  lock_sv_qs_mutex(server.sv_qs_mutex, __func__);


  for (xmlNodePtr current_node = root_node->xmlChildrenNode;
       current_node != NULL;
       current_node = current_node->next)
    {
    std::string parent(((char*)current_node->name));
    if (parent != "text")//check for filler nodes
      {
      xmlChar *content = xmlNodeGetContent(current_node);
      std::string child((char*)content);

      if (parent == "numjobs")
        {
        server.sv_qs.sv_numjobs = atoi(child.c_str());
        }
      else if (parent == "numque")
        {
        server.sv_qs.sv_numque = atoi(child.c_str());
        }
      else if (parent == "nextjobid")
        {
        server.sv_qs.sv_jobidnumber = atoi(child.c_str());
        }
      else if (parent == "savetime")
        {
        server.sv_qs.sv_savetm = atol(child.c_str());
        }
      
      else if (parent == "attributes")
        {
      
        for (xmlNodePtr attr_children = current_node->xmlChildrenNode;
             attr_children != NULL;
             attr_children = attr_children->next)
        {
        std::string attr_parent((char*)attr_children->name);
        if (attr_parent != "text")
          {
          xmlChar *attr_content = xmlNodeGetContent(attr_children);
          char *attr_child = (char *)attr_content;
          if (attr_parent == ATTR_rescavail)
            {
            for (xmlNodePtr res_child_node = attr_children->xmlChildrenNode;
                 res_child_node != NULL;
                 res_child_node = res_child_node->next)
              {
              std::string res_parent =((char*)res_child_node->name);
              if (res_parent != "text")
                {
                xmlChar *res_child_content = xmlNodeGetContent(res_child_node);
                std::string res_child((char*)res_child_content);
                xmlFree(res_child_content);

                res_parent.insert(0,"<");
                res_parent.insert(res_parent.end(),'>');
                res_child.insert(0,res_parent);
                res_parent.insert(1,"/");
                res_child.insert(res_child.end(),res_parent.begin(),res_parent.end());

                if ((rc = str_to_attr(attr_parent.c_str(), res_child.c_str(),
                        server.sv_attr,svr_attr_def,SRV_ATR_LAST)))
                  {
                  /* ERROR */
                  errorCount++;
                  snprintf(log_buf,sizeof(log_buf),
                      "Error creating resource attribute %s",
                      attr_parent.c_str());
                  log_err(rc, __func__, log_buf);
  
                  xmlFree(attr_content);
                  xmlFree(content);
                 break;
                 }
               }
             }
           }//end attr_parent == ATTR_rescavail


           
        else if ((rc = str_to_attr(attr_parent.c_str(),attr_child,server.sv_attr,svr_attr_def,SRV_ATR_LAST)))
          {
          /* ERROR */
          errorCount++;
          snprintf(log_buf,sizeof(log_buf),
            "Error creating attribute %s",
            attr_parent.c_str());

          log_err(rc, __func__, log_buf);
          
          xmlFree(attr_content);
          xmlFree(content);

          break;
          }

        if (!strcmp(attr_parent.c_str(), ATTR_tcptimeout))
          pbs_tcp_timeout = strtol(attr_child, NULL, 10);
        else if (!strcmp(attr_parent.c_str(), ATTR_tcpincomingtimeout))
          pbs_incoming_tcp_timeout = strtol(attr_child, NULL, 10);
        }
      }
    }//parent == attributes
  }//parent != text
}//node for loop

xmlFreeDoc(doc);

  if (errorCount)
    return -1;
    
  if (!read_only)
    {
    server.sv_attr[SRV_ATR_NextJobNumber].at_val.at_long = 
      server.sv_qs.sv_jobidnumber;
    
    server.sv_attr[SRV_ATR_NextJobNumber].at_flags |= 
      ATR_VFLAG_SET| ATR_VFLAG_MODIFY;
    }

  unlock_sv_qs_mutex(server.sv_qs_mutex, __func__);

  return(PBSE_NONE);
  } /* END svr_recov_xml() */

int svr_save_xml(

  struct server *ps,
  int            mode)

  {

  int     fds;
  int     rc = PBSE_NONE;
  time_t  time_now = time(NULL);
  char   *tmp_file = NULL;
  int     tmp_file_len = 0;
  char log_buf[LOCAL_LOG_BUF_SIZE + 1];
  std::ostringstream node_value; 
  
  tmp_file_len = strlen(path_svrdb) + 5;
  if ((tmp_file = (char *)calloc(sizeof(char), tmp_file_len)) == NULL)
    {
    rc = PBSE_MEM_MALLOC;
    return(rc);
    }
  else
    {
    snprintf(tmp_file, tmp_file_len - 1, "%s.tmp", path_svrdb);
    }

  lock_sv_qs_mutex(server.sv_qs_mutex, __func__);
  fds = open(tmp_file, O_WRONLY | O_CREAT | O_Sync | O_TRUNC, 0600);

  if (fds < 0)
    {
    sprintf(log_buf, "%s:1", __func__);
    unlock_sv_qs_mutex(server.sv_qs_mutex, log_buf);
    log_err(errno, __func__, msg_svdbopen);

    free(tmp_file);
    return(-1);
    }

  xmlDocPtr doc = NULL;
  doc = xmlNewDoc(BAD_CAST "1.0");
  xmlNodePtr root_node = xmlNewNode(NULL, BAD_CAST "server_db");
  xmlDocSetRootElement(doc,root_node);
  xmlNodePtr node = root_node;
  
  node_value<<ps->sv_qs.sv_numjobs;
  xmlNewChild(node,NULL,BAD_CAST "numjobs",BAD_CAST (node_value.str().c_str())); 
  node_value.str("");

  node_value<<ps->sv_qs.sv_numque;
  xmlNewChild(node,NULL,BAD_CAST "numque",BAD_CAST (node_value.str().c_str()));
  node_value.str("");

  node_value<<ps->sv_qs.sv_jobidnumber;
  xmlNewChild(node,NULL,BAD_CAST "nextjobid",BAD_CAST (node_value.str().c_str()));
  node_value.str("");

  node_value<<time_now;
  xmlNewChild(node,NULL,BAD_CAST "savetime",BAD_CAST (node_value.str().c_str()));
  node_value.str("");

  xmlNewChild(node,NULL,BAD_CAST "attributes",NULL);

  if ((rc = save_attr_xml(svr_attr_def,ps->sv_attr,SRV_ATR_LAST,node)) != 0)
    {
    sprintf(log_buf, "%s:3", __func__);
    unlock_sv_qs_mutex(server.sv_qs_mutex, log_buf);
    free(tmp_file);
    close(fds);
    return(rc);
    }
 
  xmlBuffer *xml_buffer = xmlBufferCreate();
  xmlOutputBuffer *outputBuffer = xmlOutputBufferCreateBuffer(xml_buffer, NULL);
  xmlSaveFormatFileTo(outputBuffer, doc, "utf-8",1);
  std::string xml_out_str((char*)xml_buffer->content, xml_buffer->use);
  
  if ((rc = write_buffer(xml_out_str.c_str(),xml_out_str.size(),fds)))
    {
    sprintf(log_buf, "%s:4", __func__);
    unlock_sv_qs_mutex(server.sv_qs_mutex, log_buf);
    close(fds);
    free(tmp_file);
    return(rc);
    }

  /* Some write() errors may not show up until the close() */
  if ((rc = close(fds)) != 0)
    {
    log_err(rc, __func__, "PBS got an error closing the serverdb file");
    sprintf(log_buf, "%s:5", __func__);
    unlock_sv_qs_mutex(server.sv_qs_mutex, log_buf);
    free(tmp_file);
    return(rc);
    }

  if ((rc = rename(tmp_file, path_svrdb)) == -1)
    {
    rc = PBSE_CAN_NOT_MOVE_FILE;
    }

  sprintf(log_buf, "%s:5", __func__);
  unlock_sv_qs_mutex(server.sv_qs_mutex, log_buf);

  free(tmp_file);
  xmlFreeDoc(doc);
  return(PBSE_NONE);
  } /* END svr_save_xml */





/**
 * Save the state of the server (server structure).
 *
 * @param ps     A pointer to the server state structure.
 * @param mode   This is either SVR_SAVE_QUICK or SVR_SAVE_FULL.
 * @return       Zero on success or -1 on error.
 */

int svr_save(

  struct server *ps,
  int            mode)

  {
  return(svr_save_xml(ps,mode));
  }  /* END svr_save() */




/**
 * Save an Access Control List to its file.
 *
 * @param attr   A pointer to an acl (access control list) pbs_attribute.
 * @param pdef   A pointer to the pbs_attribute definition structure.
 * @param subdir The sub-directory path specifying where to write the file.
 * @param name   The parent object name which in this context becomes the file name.
 * @return       Zero on success (File may not be written if pbs_attribute is clean) or -1 on error.
 */

int save_acl(

  pbs_attribute *attr,  /* acl pbs_attribute */
  attribute_def *pdef,  /* pbs_attribute def structure */
  const char   *subdir, /* sub-directory path */
  const char   *name)  /* parent object name = file name */

  {
  static const char *this_function_name = "save_acl";
  int          fds;
  char         filename1[MAXPATHLEN];
  char         filename2[MAXPATHLEN];
  tlist_head   head;
  int          i;
  svrattrl    *pentry;
  char         log_buf[LOCAL_LOG_BUF_SIZE];

  if ((attr->at_flags & ATR_VFLAG_MODIFY) == 0)
    {
    return(0);   /* Not modified, don't bother */
    }

  attr->at_flags &= ~ATR_VFLAG_MODIFY;

  snprintf(filename1, sizeof(filename1), "%s%s/%s",
    path_priv, subdir, name);

  if ((attr->at_flags & ATR_VFLAG_SET) == 0)
    {
    /* has been unset, delete the file */

    unlink(filename1);

    return(0);
    }

  snprintf(filename2, sizeof(filename2), "%s.new",
    filename1);

  fds = open(filename2, O_WRONLY | O_CREAT | O_TRUNC | O_Sync, 0600);

  if (fds < 0)
    {
    snprintf(log_buf, sizeof(log_buf), "unable to open acl file '%s'", filename2);

    log_err(errno, this_function_name, log_buf);

    return(-1);
    }

  CLEAR_HEAD(head);

  i = pdef->at_encode(attr, &head, pdef->at_name, (char *)0, ATR_ENCODE_SAVE, ATR_DFLAG_ACCESS);

  if (i < 0)
    {
    log_err(-1, this_function_name, (char *)"unable to encode acl");

    close(fds);

    unlink(filename2);

    return(-1);
    }

  pentry = (svrattrl *)GET_NEXT(head);

  if (pentry != NULL)
    {
    /* write entry, but without terminating null */

    while ((i = write_ac_socket(fds, pentry->al_value, pentry->al_valln - 1)) != pentry->al_valln - 1)
      {
      if ((i == -1) && (errno == EINTR))
        continue;

      log_err(errno, this_function_name, (char *)"wrote incorrect amount");

      close(fds);

      unlink(filename2);

      return(-1);
      }

    free(pentry);
    }

  close(fds);

  unlink(filename1);

  if (link(filename2, filename1) < 0)
    {
    log_err(errno, this_function_name, (char *)"unable to relink file");

    return(-1);
    }

  unlink(filename2);

  attr->at_flags &= ~ATR_VFLAG_MODIFY; /* clear modified flag */

  return(0);
  }




/**
 * Reload an Access Control List from its file.
 *
 * @param attr   A pointer to an acl (access control list) pbs_attribute.
 * @param pdef   A pointer to the pbs_attribute definition structure.
 * @param subdir The sub-directory path specifying where to read the file.
 * @param name   The parent object name which in this context is the file name.
 * @return       Zero on success (File may not be written if pbs_attribute is clean) or -1 on error.
 */

void recov_acl(

  pbs_attribute *pattr, /* acl pbs_attribute */
  attribute_def *pdef, /* pbs_attribute def structure */
  const char  *subdir, /* directory path */
  const char  *name) /* parent object name = file name */

  {
  static const char   *this_function_name = "recov_acl";
  char          *buf;
  int            fds;
  char           filename1[MAXPATHLEN];
  char           log_buf[LOCAL_LOG_BUF_SIZE];

  struct stat    sb;
  pbs_attribute  tempat;

  errno = 0;

  if (subdir != NULL)
    snprintf(filename1, sizeof(filename1), "%s%s/%s", path_priv, subdir, name);
  else
    snprintf(filename1, sizeof(filename1), "%s%s", path_priv, name);

  fds = open(filename1, O_RDONLY, 0600);

  if (fds < 0)
    {
    if (errno != ENOENT)
      {
      sprintf(log_buf, "unable to open acl file %s", filename1);

      log_err(errno, this_function_name, log_buf);
      }

    return;
    }

  if (fstat(fds, &sb) < 0)
    {
    close(fds);

    return;
    }

  if (sb.st_size == 0)
    {
    close(fds);

    return;  /* no data */
    }

  buf = (char *)calloc(1, (size_t)sb.st_size + 1); /* 1 extra for added null */

  if (buf == NULL)
    {
    close(fds);

    return;
    }

  if (read_ac_socket(fds, buf, (unsigned int)sb.st_size) != (int)sb.st_size)
    {
    log_err(errno, this_function_name, (char *)"unable to read acl file");

    close(fds);
    free(buf);

    return;
    }

  close(fds);

  *(buf + sb.st_size) = '\0';

  clear_attr(&tempat, pdef);

  if (pdef->at_decode(&tempat, pdef->at_name, NULL, buf, ATR_DFLAG_ACCESS) < 0)
    {
    sprintf(log_buf, "decode of acl %s failed", pdef->at_name);

    log_err(errno, this_function_name, log_buf);
    }
  else if (pdef->at_set(pattr, &tempat, SET) != 0)
    {
    sprintf(log_buf, "set of acl %s failed", pdef->at_name);

    log_err(errno, this_function_name, log_buf);
    }

  pdef->at_free(&tempat);

  free(buf);

  return;
  }  /* END recov_acl() */

/* END svr_recov.c  */

