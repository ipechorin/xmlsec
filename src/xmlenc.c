/** 
 * XMLSec library
 *
 * "XML Encryption" implementation
 *  http://www.w3.org/TR/xmlenc-core
 * 
 * See Copyright for the status of this software.
 * 
 * Author: Aleksey Sanin <aleksey@aleksey.com>
 */
#include "globals.h"

#ifndef XMLSEC_NO_XMLENC
 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libxml/tree.h>
#include <libxml/parser.h> 

#include <xmlsec/xmlsec.h>
#include <xmlsec/xmltree.h>
#include <xmlsec/keys.h>
#include <xmlsec/keysInternal.h>
#include <xmlsec/transforms.h>
#include <xmlsec/transformsInternal.h>
#include <xmlsec/base64.h>
#include <xmlsec/membuf.h>
#include <xmlsec/keyinfo.h>
#include <xmlsec/io.h>
#include <xmlsec/xmlenc.h>
#include <xmlsec/errors.h>

const xmlChar xmlSecEncTypeElement[] = "http://www.w3.org/2001/04/xmlenc#Element";
const xmlChar xmlSecEncTypeContent[] = "http://www.w3.org/2001/04/xmlenc#Content";

/* The ID attribute in XMLEnc is 'Id' */
static const xmlChar*		xmlSecEncIds[] = { BAD_CAST "Id", NULL };

typedef struct _xmlSecEncState {
    xmlSecEncCtxPtr		ctx;
    xmlSecBinTransformPtr 	first;
    xmlSecBinTransformPtr	last;
    xmlNodePtr			cipherDataNode;
    int				encrypt;
    xmlChar 			*type;
    
} xmlSecEncState, *xmlSecEncStatePtr;

/** 
 * XML Enc state
 */
static xmlSecEncStatePtr	xmlSecEncStateCreate		(xmlSecEncCtxPtr ctx, 
								 xmlNodePtr encNode,
								 int encrypt,
								 xmlSecEncResultPtr result);
static void			xmlSecEncStateDestroy		(xmlSecEncStatePtr state);
static int			xmlSecEncStateWriteResult	(xmlSecEncStatePtr state,
								 xmlNodePtr node,
								 xmlSecEncResultPtr result);
static int			xmlSecEncStateAddFirstTransform	(xmlSecEncStatePtr state,
								 xmlSecTransformPtr transform);
static int			xmlSecEncStateAddTransform	(xmlSecEncStatePtr state,
								 xmlSecTransformPtr transform);

#define xmlSecEncResultIsValid(result) \
	    ((( result ) != NULL) \
	    && ((result)->ctx != NULL) \
	    && ((result)->ctx->keysMngrCtx != NULL) )
#define xmlSecEncResultGetKeyCallback(result) \
	    ( ( xmlSecEncResultIsValid((result)) ) ? \
		((result)->ctx->keysMngrCtx->keysMngr->getKey) : \
		NULL )


static int			xmlSecEncryptedDataNodeRead	(xmlNodePtr encNode,
								 xmlSecEncStatePtr state, 
								 xmlSecEncResultPtr result);
static int			xmlSecCipherDataNodeRead	(xmlNodePtr cipherDataNode,
								 xmlSecEncStatePtr state, 							 
								 xmlSecEncResultPtr result);
static int 			xmlSecCipherDataNodeWrite	(xmlNodePtr cipherDataNode,
								 const unsigned char *buf,
								 size_t size);
static int			xmlSecCipherValueNodeRead	(xmlNodePtr cipherValueNode,
								 xmlSecEncStatePtr state, 
								 xmlSecEncResultPtr result);
static int			xmlSecCipherReferenceNodeRead	(xmlNodePtr cipherReferenceNode, 
								 xmlSecEncStatePtr state, 
								 xmlSecEncResultPtr result);


/****************************************************************************
 *
 * XML Encrypiton context methods
 *
 ***************************************************************************/
/**
 * xmlSecEncCtxCreate:
 * @keysMngr: the pointer to #xmlSecKeysMngr structure.
 * 
 * Creates new encryption context.
 *
 * Returns newly allocated #xmlSecEncCtx structure or NULL if an error occurs.
 */
xmlSecEncCtxPtr		
xmlSecEncCtxCreate(xmlSecKeysMngrPtr keysMngr) {
    xmlSecEncCtxPtr ctx;
    
    /*
     * Allocate a new xmlSecEncCtx and fill the fields.
     */
    ctx = (xmlSecEncCtxPtr) xmlMalloc(sizeof(xmlSecEncCtx));
    if(ctx == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_MALLOC_FAILED,
		    "sizeof(xmlSecEncCtx)=%d", 
		    sizeof(xmlSecEncCtx));
	return(NULL);
    }
    memset(ctx, 0, sizeof(xmlSecEncCtx));

    ctx->keysMngrCtx = xmlSecKeysMngrCtxCreate(keysMngr);
    if(ctx->keysMngrCtx == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecKeysMngrCtxCreate");
	xmlSecEncCtxDestroy(ctx);
	return(NULL);
    }
    return(ctx);
}

/**
 * xmlSecEncCtxDestroy:
 * @ctx: the pointer to #xmlSecEncCtx structure.
 *
 * Destroys the #xmlSecEncCtx structure.
 */
void
xmlSecEncCtxDestroy(xmlSecEncCtxPtr ctx) {
    xmlSecAssert(ctx != NULL);

    if(ctx->keysMngrCtx != NULL) {
	xmlSecKeysMngrCtxDestroy(ctx->keysMngrCtx);
    }
    
    memset(ctx, 0, sizeof(xmlSecEncCtx));
    xmlFree(ctx);
}


/**************************************************************************
 * 
 * Encryption Templates
 *
 *************************************************************************/
 
/** 
 * xmlSecEncDataCreate:
 * @id: the Id attribute (optional).
 * @type: the Type attribute (optional)
 * @mimeType: the MimeType attribute (optional)
 * @encoding: the Encoding attribute (optional)
 *
 * Creates new <enc:EncryptedData> node for encryption template. 
 *
 * Returns the pointer newly created  <enc:EncryptedData> node or NULL 
 * if an error occurs.
 */
xmlNodePtr		
xmlSecEncDataCreate(const xmlChar *id, const xmlChar *type,
		    const xmlChar *mimeType, const xmlChar *encoding) {
    xmlNodePtr encNode;
    xmlNodePtr cipherData;
    
    encNode = xmlNewNode(NULL, BAD_CAST "EncryptedData");
    if(encNode == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XML_FAILED,
		    "xmlNewNode(EncryptedData)");
	return(NULL);	        
    }
    
    if(xmlNewNs(encNode, xmlSecEncNs, NULL) == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XML_FAILED,
		    "xmlNewNs(xmlSecEncNs)");
	return(NULL);	        	
    }
    
    if(id != NULL) {
	xmlSetProp(encNode, BAD_CAST "Id", id);
    }
    if(type != NULL) {
	xmlSetProp(encNode, BAD_CAST "Type", type);
    }
    if(mimeType != NULL) {
	xmlSetProp(encNode, BAD_CAST "MimeType", mimeType);
    }
    if(encoding != NULL) {
	xmlSetProp(encNode, BAD_CAST "Encoding", encoding);
    }
    
    cipherData = xmlSecAddChild(encNode,  BAD_CAST "CipherData", xmlSecEncNs);
    if(cipherData == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecAddChild(CipherData)");
	return(NULL);	        	
    }
    
    return(encNode);
}

/** 
 * xmlSecEncDataDestroy:
 * @encNode: the pointer to <enc:EncryptedData> node.
 *
 * Destroys the <enc:EncryptedData> node @encNode.
 */
void
xmlSecEncDataDestroy(xmlNodePtr encNode) {
    xmlSecAssert(encNode != NULL);

    xmlUnlinkNode(encNode);
    xmlFreeNode(encNode);
}

/** 
 * xmlSecEncDataAddEncMethod:
 * @encNode: the pointer to <enc:EncryptedData> node.
 * @encMethod: the encryption method id.
 *
 * Adds <enc:EncryptionMethod> node with specified encryption 
 * algorithm (@encMethodId) to the  <enc:EncryptedData> node @encNode.
 *
 * Returns the pointer to newly created <enc:EncryptionMethod> node or 
 * NULL if an error occurs.
 */
xmlNodePtr
xmlSecEncDataAddEncMethod(xmlNodePtr encNode, xmlSecTransformId encMethod) {
    xmlNodePtr encMethodNode;
    xmlNodePtr tmp;
    int ret;

    xmlSecAssert2(encNode != NULL, NULL);
    xmlSecAssert2(encMethod != NULL, NULL);
    
    encMethodNode = xmlSecFindChild(encNode, BAD_CAST "EncryptionMethod", xmlSecEncNs);
    if(encMethodNode != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_NODE_ALREADY_PRESENT,
		    "EncryptionMethod");
	return(NULL);	
    }
    
    tmp = xmlSecGetNextElementNode(encNode->children);
    if(tmp == NULL) {
	encMethodNode = xmlSecAddChild(encNode,  BAD_CAST "EncryptionMethod", xmlSecEncNs);
    } else {
	encMethodNode = xmlSecAddPrevSibling(tmp,  BAD_CAST "EncryptionMethod", xmlSecEncNs);
    }    
    if(encMethodNode == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecAddChild(EncryptionMethod)");
	return(NULL);	
    }
    
    ret = xmlSecTransformNodeWrite(encMethodNode, encMethod);
    if(ret < 0){
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecTransformNodeWrite(encMethodNode) - %d", ret);
	xmlUnlinkNode(encMethodNode);
	xmlFreeNode(encMethodNode);
	return(NULL);	
    }
    return(encMethodNode);
}

/** 
 * xmlSecEncDataAddKeyInfo:
 * @encNode: the pointer to <enc:EncryptedData> node.
 *
 * Adds <dsig:KeyInfo> to the  <enc:EncryptedData> node @encNode.
 *
 * Returns the pointer to newly created <dsig:KeyInfo> node or 
 * NULL if an error occurs.
 */
xmlNodePtr
xmlSecEncDataAddKeyInfo(xmlNodePtr encNode) {
    xmlNodePtr keyInfo;
    xmlNodePtr prev;
    xmlNodePtr tmp;
        
    xmlSecAssert2(encNode != NULL, NULL);

    keyInfo = xmlSecFindChild(encNode, BAD_CAST "KeyInfo", xmlSecDSigNs);
    if(keyInfo != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_NODE_ALREADY_PRESENT,
		    "KeyInfo");
	return(NULL);	
    }
    
    prev = xmlSecFindChild(encNode, BAD_CAST "EncryptionMethod", xmlSecEncNs);
    tmp = xmlSecGetNextElementNode(encNode->children);
    if(prev != NULL) {
	keyInfo = xmlSecAddNextSibling(prev, BAD_CAST "KeyInfo", xmlSecDSigNs);
    } else if(tmp == NULL) {
	keyInfo = xmlSecAddChild(encNode, BAD_CAST "KeyInfo", xmlSecDSigNs);
    } else {
	keyInfo = xmlSecAddPrevSibling(tmp, BAD_CAST "KeyInfo", xmlSecDSigNs);
    }
    if(keyInfo == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecAddChild(KeyInfo)");
	return(NULL);	
    }
    return(keyInfo);
}

/** 
 * xmlSecEncDataAddEncProperties
 * @encNode: the pointer to <enc:EncryptedData> node.
 * @id: the Id attribute (optional).
 *
 * Adds <enc:EncryptionProperties> node to the <enc:EncryptedData> 
 * node @encNode.
 *
 * Returns the pointer to newly created <enc:EncryptionProperties> node or 
 * NULL if an error occurs.
 */
xmlNodePtr
xmlSecEncDataAddEncProperties(xmlNodePtr encNode, const xmlChar *id) {
    xmlNodePtr encProps;

    xmlSecAssert2(encNode != NULL, NULL);

    encProps = xmlSecFindChild(encNode, BAD_CAST "EncryptionProperties", xmlSecEncNs);
    if(encProps != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_NODE_ALREADY_PRESENT,
		    "EncryptionProperties");
	return(NULL);	
    }

    encProps = xmlSecAddChild(encNode, BAD_CAST "EncryptionProperties", xmlSecEncNs);
    if(encProps == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecAddChild(EncryptionProperties)");
	return(NULL);	
    }
    if(id != NULL) {
	xmlSetProp(encProps, BAD_CAST "Id", id);
    }
    
    return(encProps);
}

/** 
 * xmlSecEncDataAddEncProperty:
 * @encNode: the pointer to <enc:EncryptedData> node.
 * @id: the Id attribute (optional).
 * @target: the Target attribute (optional).
 *
 * Adds <enc:EncryptionProperty> node (and the parent 
 * <enc:EncryptionProperties> node if required) to the 
 * <enc:EncryptedData> node @encNode.
 *
 * Returns the pointer to newly created <enc:EncryptionProperty> node or 
 * NULL if an error occurs.
 */
xmlNodePtr	
xmlSecEncDataAddEncProperty(xmlNodePtr encNode, const xmlChar *id, const xmlChar *target) {
    xmlNodePtr encProp;
    xmlNodePtr encProps;
        
    xmlSecAssert2(encNode != NULL, NULL);

    encProps = xmlSecFindChild(encNode, BAD_CAST "EncryptionProperties", xmlSecEncNs);
    if(encProps == NULL) {
	encProps = xmlSecEncDataAddEncProperties(encNode, NULL);
	if(encProps == NULL) { 
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"xmlSecEncDataAddEncProperties");
	    return(NULL);	
	}
    }

    encProp = xmlSecAddChild(encProps, BAD_CAST "EncryptionProperty", xmlSecEncNs);
    if(encProp == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecAddChild(EncryptionProperty)");
	return(NULL);	
    }
    if(id != NULL) {
	xmlSetProp(encProp, BAD_CAST "Id", id);
    }
    if(target != NULL) {
	xmlSetProp(encProp, BAD_CAST "Target", target);
    }
    
    return(encProp);
}

/** 
 * xmlSecEncDataAddCipherValue:
 * @encNode: the pointer to <enc:EncryptedData> node.
 *
 * Adds <enc:CipherValue> to the <enc:EncryptedData> node @encNode.
 *
 * Returns the pointer to newly created <enc:CipherValue> node or 
 * NULL if an error occurs.
 */
xmlNodePtr
xmlSecEncDataAddCipherValue(xmlNodePtr encNode) {
    xmlNodePtr cipherData;
    xmlNodePtr cipherValue;
    xmlNodePtr tmp;
        
    xmlSecAssert2(encNode != NULL, NULL);

    cipherData = xmlSecFindChild(encNode, BAD_CAST "CipherData", xmlSecEncNs);
    if(cipherData == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_NODE_NOT_FOUND,
		    "CipherData");
	return(NULL);	
    }

    tmp = xmlSecFindChild(cipherData, BAD_CAST "CipherValue", xmlSecEncNs);
    if(tmp != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_NODE_ALREADY_PRESENT,
		    "CipherValue");
	return(NULL);	
    }

    tmp = xmlSecFindChild(cipherData, BAD_CAST "CipherReference", xmlSecEncNs);
    if(tmp != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_NODE_ALREADY_PRESENT,
		    "CipherReference");
	return(NULL);	
    }

    cipherValue = xmlSecAddChild(cipherData, BAD_CAST "CipherValue", xmlSecEncNs);
    if(cipherValue == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecAddChild(CipherValue)");
	return(NULL);	
    }    
        
    return(cipherValue);
}

/** 
 * xmlSecEncDataAddCipherReference:
 * @encNode: the pointer to <enc:EncryptedData> node.
 * @uri: the URI attribute (may be NULL).
 *
 * Adds <enc:CipherReference> node with specified URI attribute @uri
 * to the <enc:EncryptedData> node @encNode.
 *
 * Returns the pointer to newly created <enc:CipherReference> node or 
 * NULL if an error occurs.
 */
xmlNodePtr
xmlSecEncDataAddCipherReference(xmlNodePtr encNode, const xmlChar *uri) {
    xmlNodePtr cipherRef;
    xmlNodePtr cipherData;    
    xmlNodePtr tmp;
    
    xmlSecAssert2(encNode != NULL, NULL);

    cipherData = xmlSecFindChild(encNode, BAD_CAST "CipherData", xmlSecEncNs);
    if(cipherData == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_NODE_NOT_FOUND,
		    "CipherData");
	return(NULL);	
    }

    tmp = xmlSecFindChild(cipherData, BAD_CAST "CipherValue", xmlSecEncNs);
    if(tmp != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_NODE_ALREADY_PRESENT,
		    "CipherValue");
	return(NULL);	
    }

    tmp = xmlSecFindChild(cipherData, BAD_CAST "CipherReference", xmlSecEncNs);
    if(tmp != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_NODE_ALREADY_PRESENT,
		    "CipherReference");
	return(NULL);	
    }

    cipherRef = xmlSecAddChild(cipherData, BAD_CAST "CipherReference", xmlSecEncNs);
    if(cipherRef == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecAddChild(CipherReference)");
	return(NULL);	
    }    
    
    if(uri != NULL) {
	xmlSetProp(cipherRef, BAD_CAST "URI", uri);
    }
    
    return(cipherRef);
}

/** 
 * xmlSecCipherReferenceAddTransform:
 * @encNode: the pointer to <enc:EncryptedData> node.
 * @transform: the transform id.
 *
 * Adds <dsig:Transform> node (and the parent <dsig:Transforms> node)
 * with specified transform methods @transform to the <enc:CipherReference>
 * child node of the <enc:EncryptedData> node @encNode.
 *
 * Returns the pointer to newly created <dsig:Transform> node or 
 * NULL if an error occurs.
 */
xmlNodePtr
xmlSecCipherReferenceAddTransform(xmlNodePtr encNode, 
				  xmlSecTransformId transform) {
    xmlNodePtr cipherData;
    xmlNodePtr cipherRef;    
    xmlNodePtr transforms;
    xmlNodePtr cipherRefTransform;
    int ret;

    xmlSecAssert2(encNode != NULL, NULL);
    xmlSecAssert2(transform != NULL, NULL);    

    cipherData = xmlSecFindChild(encNode, BAD_CAST "CipherData", xmlSecEncNs);
    if(cipherData == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_NODE_NOT_FOUND,
		    "CipherData");
	return(NULL);	
    }

    cipherRef = xmlSecFindChild(cipherData, BAD_CAST "CipherReference", xmlSecEncNs);
    if(cipherRef == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_NODE_NOT_FOUND,
		    "CipherReference");
	return(NULL);	
    }

    transforms = xmlSecFindChild(cipherRef, BAD_CAST "Transforms", xmlSecEncNs);
    if(transforms == NULL) {
	transforms = xmlSecAddChild(cipherRef, BAD_CAST "Transforms", xmlSecEncNs);
	if(transforms == NULL) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"xmlSecAddChild(Transforms)");
	    return(NULL);	
	}
    }
    
    cipherRefTransform = xmlSecAddChild(transforms,  BAD_CAST "Transform", xmlSecDSigNs);
    if(cipherRefTransform == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecAddChild(Transform)");
	return(NULL);	
    }
    
    ret = xmlSecTransformNodeWrite(cipherRefTransform, transform);
    if(ret < 0){
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecTransformNodeWrite(cipherRefTransform) - %d", ret);
	return(NULL);	
    }
    
    return(cipherRefTransform);
}


/*************************************************************************
 *
 * Encryption functions
 *
 *************************************************************************/

/**
 * xmlSecEncryptMemory:
 * @ctx: the pointer to #xmlSecEncCtx structure.
 * @key: the key to use (if NULL then the key specified in <dsig:KeyInfo>
 *     will be used).   
 * @encNode: the pointer to encryption template (<enc:EncryptionData> node).
 * @buf: the pointer to data to encrypt.
 * @size: the size of the data in @buf.
 * @result: the pointer where to store encryption results.
 *
 * Encrypts binary data from the @buf according to the template in the
 * <enc:EncryptionData> node. After the encrytion the result XML is in 
 * the @encNode node.
 *
 * Returns 0 on success or a negative value otherwise.
 */
int
xmlSecEncryptMemory(xmlSecEncCtxPtr ctx, xmlSecKeyPtr key, 
		    xmlNodePtr encNode, const unsigned char *buf, size_t size,
		    xmlSecEncResultPtr *result) {
    xmlSecEncStatePtr state = NULL;
    xmlSecEncResultPtr res = NULL;
    int ret;

    xmlSecAssert2(encNode != NULL, -1);
    xmlSecAssert2(ctx != NULL, -1);    
    xmlSecAssert2(buf != NULL, -1);    
    
    res = xmlSecEncResultCreate(ctx, 1, encNode);
    if(res == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecEncResultCreate");
	return(-1);	    		
    }
    if(key != NULL) {
	res->key = xmlSecKeyDuplicate(key);    
	/* todo: check key */
	res->key->origin = key->origin;    
    }
    
    /* add ids for Encrypted nodes */
    xmlSecAddIDs(encNode->doc, encNode, xmlSecEncIds);
    
    /**
     * create state
     */    
    state = xmlSecEncStateCreate(ctx, encNode, 1, res);
    if(state == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecEncStateCreate");
	xmlSecEncResultDestroy(res);
	return(-1);	    
    }
         
    /* encrypt the data */
    ret = xmlSecBinTransformWrite((xmlSecTransformPtr)state->first, buf, size);
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecBinTransformWrite - %d", ret);
	xmlSecEncResultDestroy(res);
	xmlSecEncStateDestroy(state);    
	return(-1);	    	
    }
    ret = xmlSecBinTransformFlush((xmlSecTransformPtr)state->first);
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecBinTransformFlush - %d", ret);
	xmlSecEncResultDestroy(res);
	xmlSecEncStateDestroy(state);    
	return(-1);	    	
    }

    ret = xmlSecEncStateWriteResult(state, encNode, res); 			
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecEncStateWriteResult - %d", ret);
	xmlSecEncStateDestroy(state);
	xmlSecEncResultDestroy(res); 
	return(-1);	    
    }
    
    /* cleanup */
    if(result != NULL) {
	(*result) = res;
    } else {
	xmlSecEncResultDestroy(res);
    }
    xmlSecEncStateDestroy(state);    
    return(0);
}

/**
 * xmlSecEncryptUri:
 * @ctx: the pointer to #xmlSecEncCtx structure.
 * @key: the key to use (if NULL then the key specified in <dsig:KeyInfo>
 *     will be used).   
 * @encNode: the pointer to encryption template (<enc:EncryptionData> node).
 * @uri: the URI to data to encrypt.
 * @result: the pointer where to store encryption results.
 *
 * Encrypts binary data from the @uri according to the template in the
 * <enc:EncryptionData> node. After the encrytion the result XML is in 
 * the @encNode node.
 *
 * Returns 0 on success or a negative value otherwise.
 */
int
xmlSecEncryptUri(xmlSecEncCtxPtr ctx, xmlSecKeyPtr key, 
		xmlNodePtr encNode, const char *uri, 
		xmlSecEncResultPtr *result) {
    xmlSecEncStatePtr state = NULL;
    xmlSecEncResultPtr res = NULL;
    xmlSecTransformPtr inputUri = NULL;
    unsigned char buf[1024];
    int ret;

    xmlSecAssert2(encNode != NULL, -1);
    xmlSecAssert2(ctx != NULL, -1);    
    xmlSecAssert2(uri != NULL, -1);    

    res = xmlSecEncResultCreate(ctx, 1, encNode);
    if(res == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecEncResultCreate");
	return(-1);	    		
    }
    if(key != NULL) {
	res->key = xmlSecKeyDuplicate(key);
	/* todo: check key */
	res->key->origin = key->origin;    
    }

    /* add ids for Encrypted nodes */
    xmlSecAddIDs(encNode->doc, encNode, xmlSecEncIds);

    /**
     * create state
     */    
    state = xmlSecEncStateCreate(ctx, encNode, 1, res);
    if(state == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecEncStateCreate");
	xmlSecEncResultDestroy(res);
	return(-1);	    
    }
    
    /* add the uri load at the beginning */
    inputUri = xmlSecTransformCreate(xmlSecInputUri, 0, 0);
    if(inputUri == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecTransformCreate");
	xmlSecEncResultDestroy(res);
	xmlSecEncStateDestroy(state);    
	return(-1);	
    }    
    
    ret = xmlSecInputUriTransformOpen(inputUri, uri);
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecInputUriTransformOpen(%s) - %d", uri, ret);
	xmlSecTransformDestroy(inputUri, 1);	
	xmlSecEncResultDestroy(res);
	xmlSecEncStateDestroy(state);    
	return(-1);	
    }
    
    ret = xmlSecEncStateAddFirstTransform(state, inputUri);
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecEncStateAddFirstTransform - %d", ret);
	xmlSecTransformDestroy(inputUri, 1);	
	xmlSecEncResultDestroy(res);
	xmlSecEncStateDestroy(state);    
	return(-1);	
    }
         
    /* encrypt the data */
    do {
	ret = xmlSecBinTransformRead((xmlSecTransformPtr)state->last, buf, sizeof(buf));
    } while(ret > 0);
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecBinTransformRead - %d", ret);
	xmlSecEncResultDestroy(res);
	xmlSecEncStateDestroy(state);    
	return(-1);	    	
    }

    ret = xmlSecEncStateWriteResult(state, encNode, res); 			
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecEncStateWriteResult - %d", ret);
	xmlSecEncStateDestroy(state);
	xmlSecEncResultDestroy(res); 
	return(-1);	    
    }
    
    /* cleanup */
    if(result != NULL) {
	(*result) = res;
    } else {
	xmlSecEncResultDestroy(res);
    }
    xmlSecEncStateDestroy(state);    
    return(0);
}

/**
 * xmlSecEncryptXmlNode:
 * @ctx: the pointer to #xmlSecEncCtx structure.
 * @key: the key to use (if NULL then the key specified in <dsig:KeyInfo>
 *     will be used).   
 * @encNode: the pointer to encryption template (<enc:EncryptionData> node).
 * @src: the pointer to XML node to encrypt.
 * @result: the pointer where to store encryption results.
 *
 * Encrypts XML data from the @encNode according to the template in the
 * <enc:EncryptionData> node. After the encrytion the result XML is in 
 * the @src node.
 *
 * Returns 0 on success or a negative value otherwise.
 */
int
xmlSecEncryptXmlNode(xmlSecEncCtxPtr ctx, xmlSecKeyPtr key, 
		    xmlNodePtr encNode, xmlNodePtr src, 
		    xmlSecEncResultPtr *result) {
    xmlSecEncStatePtr state = NULL;
    xmlSecEncResultPtr res = NULL;
    xmlBufferPtr buffer = NULL;
    int ret;

    xmlSecAssert2(encNode != NULL, -1);
    xmlSecAssert2(ctx != NULL, -1);    
    xmlSecAssert2(src != NULL, -1);    

    res = xmlSecEncResultCreate(ctx, 1, encNode);
    if(res == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecEncResultCreate");
	return(-1);	    		
    }
    if(key != NULL) {
	res->key = xmlSecKeyDuplicate(key);
	/* todo: check key */
	res->key->origin = key->origin;    
    }

    /* add ids for Encrypted nodes */
    xmlSecAddIDs(encNode->doc, encNode, xmlSecEncIds);

    /**
     * create state
     */    
    state = xmlSecEncStateCreate(ctx, encNode, 1, res);
    if(state == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecEncStateCreate");
	xmlSecEncResultDestroy(res);
	return(-1);	    
    }

    buffer = xmlBufferCreate();
    if(buffer == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlBufferCreate");
	xmlSecEncResultDestroy(res);
	xmlSecEncStateDestroy(state);    
	return(-1);	    	
    }
    
    /** 
     * read the stuff we need to encrypt into the buffer 
     */
    if(ctx->ignoreType || (res->type == NULL)) {
	/* get the content of the node */
	xmlNodeDump(buffer, src->doc, src, 0, 0);
    } else if(xmlStrEqual(res->type, xmlSecEncTypeElement)) {
	/* get the content of the node */
	xmlNodeDump(buffer, src->doc, src, 0, 0);
    } else if(xmlStrEqual(res->type, xmlSecEncTypeContent)) {
	xmlNodePtr ptr;
	/* get the content of the nodes childs */
	ptr = src->children;
	while(ptr != NULL) {
	    xmlNodeDump(buffer, ptr->doc, ptr, 0, 0); 
	    ptr = ptr->next;
	}
    } else {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TYPE,
		    "type \"%s\" is unknown", res->type);	
	xmlBufferFree(buffer);
	xmlSecEncResultDestroy(res);
	xmlSecEncStateDestroy(state);    
	return(-1);	    	
    }
        
    /* encrypt the data */
    ret = xmlSecBinTransformWrite((xmlSecTransformPtr)state->first, 
				  xmlBufferContent(buffer),
				  xmlBufferLength(buffer));
    xmlBufferFree(buffer); buffer = NULL;
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecBinTransformWrite - %d", ret);
	xmlSecEncResultDestroy(res);
	xmlSecEncStateDestroy(state);    
	return(-1);	    	
    }
    ret = xmlSecBinTransformFlush((xmlSecTransformPtr)state->first);
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecBinTransformFlush - %d", ret);
	xmlSecEncResultDestroy(res);
	xmlSecEncStateDestroy(state);    
	return(-1);	    	
    }

    ret = xmlSecEncStateWriteResult(state, encNode, res); 			
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecEncStateWriteResult - %d", ret);
	xmlSecEncStateDestroy(state);
	xmlSecEncResultDestroy(res); 
	return(-1);	    
    }
    
    if(!ctx->ignoreType && (res->type != NULL)) {
	if(xmlStrEqual(res->type, xmlSecEncTypeElement)) {
	    ret = xmlSecReplaceNode(src, encNode);
	    if(ret < 0) {
		xmlSecError(XMLSEC_ERRORS_HERE,
			    XMLSEC_ERRORS_R_XMLSEC_FAILED,
			    "xmlSecReplaceNode - %d", ret);
		xmlSecEncStateDestroy(state);
		xmlSecEncResultDestroy(res); 
		return(-1);
	    }
	    res->replaced = 1;			       
	} else if(xmlStrEqual(res->type, xmlSecEncTypeContent)) {
	    ret = xmlSecReplaceContent(src, encNode);
	    if(ret < 0) {
		xmlSecError(XMLSEC_ERRORS_HERE,
			    XMLSEC_ERRORS_R_XMLSEC_FAILED,
			    "xmlSecReplaceContent - %d", ret);
		xmlSecEncStateDestroy(state);
		xmlSecEncResultDestroy(res); 
		return(-1);
	    }
	    res->replaced = 1;			       
	} else {
	    /* do nothing: type is unknown */
	}
    }
    
    /* cleanup */
    if(result != NULL) {
	(*result) = res;
    } else {
	xmlSecEncResultDestroy(res);
    }
    xmlSecEncStateDestroy(state);    
    return(0);
}

/************************************************************************
 *
 * Decryption function
 *
 ***********************************************************************/
 
/**
 * xmlSecDecrypt:
 * @ctx: the pointer to #xmlSecEncCtx structure.
 * @key: the key to use (if NULL then the key specified in <dsig:KeyInfo>
 *     will be used).   
 * @encNode: the pointer to encryption template (<enc:EncryptionData> node).
 * @result: the pointer where to store encryption results.
 *
 * Decrypts data from the <enc:EncryptionData> node. 
 *
 * Returns 0 on success or a negative value otherwise.
 */
int
xmlSecDecrypt(xmlSecEncCtxPtr ctx, xmlSecKeyPtr key, 
	     xmlNodePtr encNode, xmlSecEncResultPtr *result) {
    xmlSecEncStatePtr state;
    xmlSecEncResultPtr res;
    int ret;

    xmlSecAssert2(encNode != NULL, -1);
    xmlSecAssert2(ctx != NULL, -1);    

    /* first of all, create result and encryption state objects */
    res = xmlSecEncResultCreate(ctx, 0, encNode);
    if(res == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecEncResultCreate");
	return(-1);	    		
    }
    if(key != NULL) {
	res->key = xmlSecKeyDuplicate(key);
    	/* todo: check key */
	res->key->origin = key->origin;    
    }

    /* add ids for Encrypted nodes */
    xmlSecAddIDs(encNode->doc, encNode, xmlSecEncIds);

    state = xmlSecEncStateCreate(ctx, encNode, 0, res);
    if(state == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecEncStateCreate");
	xmlSecEncResultDestroy(res);
	return(-1);	    
    }
    
    if(state->cipherDataNode == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_NODE_NOT_FOUND,
		    "CipherData");
	xmlSecEncResultDestroy(res);
	xmlSecEncStateDestroy(state);    	
	return(-1);	    	
    }
    
    ret = xmlSecCipherDataNodeRead(state->cipherDataNode, state, res);
    if((ret < 0) || (res->buffer == NULL)){
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecCipherDataNodeRead - %d", ret);
	xmlSecEncResultDestroy(res);
	xmlSecEncStateDestroy(state);    	
	return(-1);	    	
    }
    
    if(!ctx->ignoreType && (res->type != NULL)) {
	if(xmlStrEqual(res->type, xmlSecEncTypeElement)) {
	    ret = xmlSecReplaceNodeBuffer(encNode, 
				    xmlBufferContent(res->buffer),  
				    xmlBufferLength(res->buffer));
	    if(ret < 0) {
		xmlSecError(XMLSEC_ERRORS_HERE,
			    XMLSEC_ERRORS_R_XMLSEC_FAILED,
			    "xmlSecReplaceNodeBuffer - %d", ret);
		xmlSecEncResultDestroy(res);
		xmlSecEncStateDestroy(state);    
		return(-1);	    	
	    }
	    res->replaced = 1;			       
	} else if(xmlStrEqual(res->type, xmlSecEncTypeContent)) {
	    /* replace the node with the buffer */
	    ret = xmlSecReplaceNodeBuffer(encNode, 
				       xmlBufferContent(res->buffer),
				       xmlBufferLength(res->buffer));
	    if(ret < 0) {
		xmlSecError(XMLSEC_ERRORS_HERE,
			    XMLSEC_ERRORS_R_XMLSEC_FAILED,
			    "xmlSecReplaceNodeBuffer - %d", ret);
		xmlSecEncResultDestroy(res);
		xmlSecEncStateDestroy(state);    
		return(-1);	    	
	    }	
	    res->replaced = 1;			       
	} else {
	    /* do nothing: type is unknown */
	}
    }
    
    /* cleanup */
    if(result != NULL) {
	(*result) = res;
    } else {
	xmlSecEncResultDestroy(res);
    }
    xmlSecEncStateDestroy(state);    
    return(0);
}
							 
/*************************************************************************
 *
 * XMLS Enc state
 *
 ************************************************************************/

/** 
 * xmlSecEncStateCreate:
 */ 
static xmlSecEncStatePtr
xmlSecEncStateCreate(xmlSecEncCtxPtr ctx, xmlNodePtr encNode, int encrypt, xmlSecEncResultPtr result) {
    xmlSecEncStatePtr state;
    int ret;

    xmlSecAssert2(encNode != NULL, NULL);
    xmlSecAssert2(ctx != NULL, NULL);    
    xmlSecAssert2(result != NULL, NULL);    

    /*
     * Allocate a new xmlSecEncState and fill the fields.
     */
    state = (xmlSecEncStatePtr) xmlMalloc(sizeof(xmlSecEncState));
    if(state == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_MALLOC_FAILED,
		    "sizeof(xmlSecEncState)=%d", 
		    sizeof(xmlSecEncState));
	return(NULL);
    }
    memset(state, 0, sizeof(xmlSecEncState));
        
    state->encrypt = encrypt;
    state->ctx = ctx;

    /*  read and update the Encryption Method and KeyInfo */
    ret = xmlSecEncryptedDataNodeRead(encNode, state, result);
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecEncryptedDataNodeRead - %d", ret);
	xmlSecEncStateDestroy(state);
	return(NULL);	    	
    }

    return(state);
}

/** 
 * xmlSecEncStateDestroy:
 */ 
static void
xmlSecEncStateDestroy(xmlSecEncStatePtr state) {
    xmlSecAssert(state != NULL);

    if(state->first != NULL) {
	xmlSecBinTransformDestroyAll((xmlSecTransformPtr)state->first);
    } else if(state->last != NULL) {
	xmlSecBinTransformDestroyAll((xmlSecTransformPtr)state->last);
    }
    
    memset(state, 0, sizeof(xmlSecEncState));
    xmlFree(state);
}

/**
 * xmlSecEncStateWriteResult:
 */
static int
xmlSecEncStateWriteResult(xmlSecEncStatePtr state, xmlNodePtr encNode,
		       xmlSecEncResultPtr result) {
    int ret;
    
    xmlSecAssert2(encNode != NULL, -1);
    xmlSecAssert2(state != NULL, -1);    
    xmlSecAssert2(result != NULL, -1);    

    /* update template */
    result->buffer = xmlSecMemBufTransformGetBuffer((xmlSecTransformPtr)state->last, 1);
    if(result->buffer == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecMemBufTransformGetBuffer");
	xmlSecEncResultDestroy(result);
	xmlSecEncStateDestroy(state);    
	return(-1);	    	
    }
    
    ret = xmlSecCipherDataNodeWrite(state->cipherDataNode, 
				xmlBufferContent(result->buffer), 
				xmlBufferLength(result->buffer));
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecCipherDataNodeWrite - %d", ret);
	xmlSecEncResultDestroy(result);
	xmlSecEncStateDestroy(state);    
	return(-1);	    	
    }
    return(0);
}

/**
 * xmlSecEncStateAddTransform:
 */
static int
xmlSecEncStateAddTransform(xmlSecEncStatePtr state, 
			    xmlSecTransformPtr transform) {

    xmlSecAssert2(state != NULL, -1);    
    xmlSecAssert2(transform != NULL, -1);    

    if(!xmlSecTransformCheckType(transform, xmlSecTransformTypeBinary)) { 
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecTransformTypeBinary");
	return(-1);	    
    }
    if(state->last == NULL) {
	state->first = state->last = (xmlSecBinTransformPtr)transform;
    } else if(xmlSecBinTransformAddAfter((xmlSecTransformPtr)state->last, 
					 transform) != NULL) {
	 state->last = (xmlSecBinTransformPtr)transform;
    } else {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecBinTransformAddAfter");
	return(-1);	    
    }
    return(0);
}

/**
 * xmlSecEncStateAddFirstTransform:
 */
static int
xmlSecEncStateAddFirstTransform(xmlSecEncStatePtr state, xmlSecTransformPtr transform) {
    xmlSecAssert2(state != NULL, -1);    
    xmlSecAssert2(transform != NULL, -1);    

    if(!xmlSecTransformCheckType(transform, xmlSecTransformTypeBinary)) { 
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecTransformTypeBinary");
	return(-1);	    
    }
    if(state->first == NULL) {
	state->first = state->last = (xmlSecBinTransformPtr)transform;
    } else if(xmlSecBinTransformAddBefore((xmlSecTransformPtr)state->first, 
					  transform) != NULL) {
	 state->first = (xmlSecBinTransformPtr)transform;
    } else {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecBinTransformAddBefore");
	return(-1);	    
    }
    return(0);
}

/**************************************************************************
 *
 * XML Enc Result functions
 *
 *************************************************************************/
/**
 * xmlSecEncResultCreate:
 * @ctx: the pointer to #xmlSecEncCtx structure.
 * @encrypt: the encrypt/decrypt flag.
 * @node: the pointer to <enc:EncryptedData> node.
 * 
 * Creates new #xmlSecEncResult structure.
 *
 * Returns newly created #xmlSecEncResult structure or NULL 
 * if an error occurs.
 */ 		
xmlSecEncResultPtr		
xmlSecEncResultCreate(xmlSecEncCtxPtr ctx, int encrypt, xmlNodePtr node) {
    xmlSecEncResultPtr result;

    xmlSecAssert2(ctx != NULL, NULL);    
    
    /*
     * Allocate a new xmlSecEncResult and fill the fields.
     */
    result = (xmlSecEncResultPtr) xmlMalloc(sizeof(xmlSecEncResult));
    if(result == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_MALLOC_FAILED,
		    "sizeof(xmlSecEncResult)=%d", 
		    sizeof(xmlSecEncResult));
	return(NULL);
    }
    memset(result, 0, sizeof(xmlSecEncResult));
        
    result->ctx = ctx;
    result->self = node;
    result->encrypt = encrypt;
    return(result);
}

/**
 * xmlSecEncResultDestroy:
 * @result: the pointer to #xmlSecEncResult structure.
 *
 * Destroys #xmlSecEncResult structure @result.
 */ 		
void
xmlSecEncResultDestroy(xmlSecEncResultPtr result) {
    xmlSecAssert(result != NULL);
    
    if(result->key != NULL) {
	xmlSecKeyDestroy(result->key);
    }

    if(result->buffer != NULL) {
	xmlBufferEmpty(result->buffer);
	xmlBufferFree(result->buffer); 	
    }

    if(result->id != NULL) {
	xmlFree(result->id);
    }
    if(result->type != NULL) {
	xmlFree(result->type);
    }
    if(result->mimeType != NULL) {
	xmlFree(result->mimeType);
    }
    if(result->encoding != NULL) {
	xmlFree(result->encoding);
    }
    
    memset(result, 0, sizeof(xmlSecEncResult));
    xmlFree(result);
}


/**
 * xmlSecEncResultDebugDump:
 * @result: the pointer to #xmlSecEncResult structure.
 * @output: the pointer to destination FILE.
 *
 * Prints the #xmlSecEncResult structure @result to file @output.
 */
void
xmlSecEncResultDebugDump(xmlSecEncResultPtr result, FILE *output) {
    xmlSecAssert(result != NULL);
    xmlSecAssert(output != NULL);

    if(result->encrypt) {    
        fprintf(output, "= ENCRYPTION RESULT\n");
    } else {
        fprintf(output, "= DECRYPTION RESULT (%s)\n",
	    (result->replaced) ? "replaced" : "not-replaced" );
    }
    if(result->id != NULL) {
	fprintf(output, "== Id: \"%s\"\n", result->id);
    }
    if(result->type != NULL) {
	fprintf(output, "== Type: \"%s\"\n", result->type);
    }
    if(result->mimeType != NULL) {
	fprintf(output, "== MimeType: \"%s\"\n", result->mimeType);
    }
    if(result->encoding != NULL) {
	fprintf(output, "== Encoding: \"%s\"\n", result->encoding);
    }
    
    if(result->key != NULL) {
	xmlSecKeyDebugDump(result->key, output);
    }

    if(result->buffer != NULL) {
	fprintf(output, "== start buffer:\n");
	fwrite(xmlBufferContent(result->buffer), 
	       xmlBufferLength(result->buffer), 1,
	       output);
	fprintf(output, "\n== end buffer\n");
    }	    

}


/**
 * xmlSecEncResultDebugXmlDump:
 * @result: the pointer to #xmlSecEncResult structure.
 * @output: the pointer to destination FILE.
 *
 * Prints the #xmlSecEncResult structure @result to file @output in XML format.
 */
void
xmlSecEncResultDebugXmlDump(xmlSecEncResultPtr result, FILE *output) {
    xmlSecAssert(result != NULL);
    xmlSecAssert(output != NULL);

    if(result->encrypt) {    
        fprintf(output, "<EncryptionResult>\n");
    } else {
        fprintf(output, "<DecryptionResult type=\"%s\">\n",
	    (result->replaced) ? "replaced" : "not-replaced" );
    }
    if(result->id != NULL) {
	fprintf(output, "<Id>%s</Id>\n", result->id);
    }
    if(result->type != NULL) {
	fprintf(output, "<Type>%s</Type>\n", result->type);
    }
    if(result->mimeType != NULL) {
	fprintf(output, "<MimeType%s</MimeType>\n", result->mimeType);
    }
    if(result->encoding != NULL) {
	fprintf(output, "<Encoding>%s</Encoding>\n", result->encoding);
    }
    
    if(result->key != NULL) {
	xmlSecKeyDebugXmlDump(result->key, output);
    }

    if(result->buffer != NULL) {
	fprintf(output, "<Buffer>");
	fwrite(xmlBufferContent(result->buffer), 
	       xmlBufferLength(result->buffer), 1,
	       output);
	fprintf(output, "</Buffer>\n");
    }	    

    if(result->encrypt) {    
        fprintf(output, "</EncryptionResult>\n");
    } else {
        fprintf(output, "</DecryptionResult>\n");
    }

}

/**
 * xmlSecEncryptedDataNodeRead:
 */
static int
xmlSecEncryptedDataNodeRead(xmlNodePtr encNode, xmlSecEncStatePtr state, xmlSecEncResultPtr result) {
    xmlNodePtr cur;
    xmlNodePtr keyInfoNode = NULL;
    xmlSecTransformPtr encryptionMethod = NULL;
    int ret;

    xmlSecAssert2(encNode != NULL, -1);
    xmlSecAssert2(state!= NULL, -1);    
    xmlSecAssert2(result != NULL, -1);
    xmlSecAssert2(result->ctx != NULL, -1);
    xmlSecAssert2(result->ctx->keysMngrCtx != NULL, -1);

    result->id = xmlGetProp(encNode, BAD_CAST "Id");
    result->type = xmlGetProp(encNode, BAD_CAST "Type");
    result->mimeType = xmlGetProp(encNode, BAD_CAST "MimeType");
    result->encoding = xmlGetProp(encNode, BAD_CAST "Encoding");
    cur = xmlSecGetNextElementNode(encNode->children);    
        
    /* first node is optional EncryptionMethod */
    if((cur != NULL) && (xmlSecCheckNodeName(cur, BAD_CAST "EncryptionMethod", xmlSecEncNs))) {
	encryptionMethod = xmlSecTransformNodeRead(cur, xmlSecUsageEncryptionMethod, 0);
	cur = xmlSecGetNextElementNode(cur->next);
    } else if((state->ctx != NULL) && (state->ctx->encryptionMethod != xmlSecTransformUnknown)){
	/* get encryption method from the context */
	encryptionMethod = xmlSecTransformCreate(state->ctx->encryptionMethod,
						xmlSecUsageEncryptionMethod, 0);
    } else {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_DATA,
		    "encryption method not specified");
	return(-1);
    }
    if(encryptionMethod == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecTransformNodeRead(EncMethod) or xmlSecTransformCreate");
	return(-1);
    }    
    ret = xmlSecEncStateAddTransform(state, encryptionMethod);
    if(ret < 0) {    
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecEncStateAddTransform - %d", ret);
	xmlSecTransformDestroy(encryptionMethod, 1); 
	return(-1);
    }
    xmlSecBinTransformSetEncrypt(encryptionMethod, state->encrypt);
    result->encryptionMethod = encryptionMethod->id;
    
    /* next node is optional KeyInfo */
    if((cur != NULL) && (xmlSecCheckNodeName(cur, BAD_CAST "KeyInfo", xmlSecDSigNs))) {
	keyInfoNode = cur;
	cur = xmlSecGetNextElementNode(cur->next);
    }    

    /* now we are ready to get key, KeyInfo node may be NULL! */
    if((result->key == NULL) && (xmlSecEncResultGetKeyCallback(result) != NULL)) {
	if(result->encrypt) {
	    result->ctx->keysMngrCtx->keyType = xmlSecBinTransformIdGetEncKeyType(result->encryptionMethod);
	    result->ctx->keysMngrCtx->keyUsage = xmlSecKeyUsageEncrypt;
	} else {
	    result->ctx->keysMngrCtx->keyType = xmlSecBinTransformIdGetDecKeyType(result->encryptionMethod);
	    result->ctx->keysMngrCtx->keyUsage = xmlSecKeyUsageDecrypt;
	}
	result->ctx->keysMngrCtx->keyId = xmlSecBinTransformIdGetKeyId(result->encryptionMethod);
		
	result->key = xmlSecEncResultGetKeyCallback(result)
					(keyInfoNode, result->ctx->keysMngrCtx); 
    }    
    if(result->key == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_KEY_NOT_FOUND,
		    " ");
	return(-1);
    }
    ret = xmlSecBinTransformAddKey(encryptionMethod, result->key->value);
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecBinTransformAddKey - %d", ret);
	return(-1);
    }
    if(result->encrypt && (keyInfoNode != NULL)) {
	/* update KeyInfo! */
	ret = xmlSecKeyInfoNodeWrite(keyInfoNode, 
			result->ctx->keysMngrCtx,
		    	result->key, 
			xmlSecBinTransformIdGetDecKeyType(result->encryptionMethod));
	if(ret < 0) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"xmlSecKeyInfoNodeWrite - %d", ret);
	    return(-1);
	}	
    }

    /* next is required CipherData node */
    if((cur == NULL) || (!xmlSecCheckNodeName(cur, BAD_CAST "CipherData", xmlSecEncNs))) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_NODE,
		    "CipherData");
	return(-1);
    }
    state->cipherDataNode = cur;
    cur = xmlSecGetNextElementNode(cur->next);

    /* next is optional EncryptionProperties node (we simply ignore it) */
    if((cur != NULL) && (xmlSecCheckNodeName(cur, BAD_CAST "EncryptionProperties", xmlSecEncNs))) {
	cur = xmlSecGetNextElementNode(cur->next);
    }

    if(state->encrypt == 1) {
	xmlSecTransformPtr memBuf = NULL;	
	xmlSecTransformPtr base64;
	/* last transform for encryption is base64 encode */
	
	base64 = xmlSecTransformCreate(xmlSecEncBase64Encode, 0, 0);
	if(base64 == NULL) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"xmlSecTransformCreate(xmlSecEncBase64Encode)");
	    return(-1);
	}
	ret = xmlSecEncStateAddTransform(state, base64);
	if(ret < 0) {    
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"xmlSecEncStateAddTransform(xmlSecEncBase64Encode) - %d", ret);
	    xmlSecTransformDestroy(base64, 1); 
	    return(-1);
	}
	
	/* add mem buf at the end */
	memBuf = xmlSecTransformCreate(xmlSecMemBuf, 0, 0);
	if(memBuf == NULL) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"xmlSecTransformCreate(xmlSecMemBuf)");
	    return(-1);
	}
	ret = xmlSecEncStateAddTransform(state, memBuf);
        if(ret < 0) {    
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"xmlSecEncStateAddTransform(xmlSecMemBuf) - %d", ret);
	    xmlSecTransformDestroy(memBuf, 1); 
	    return(-1);
	}
    }

/*    
    TODO: add support for other nodes
    if(cur != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_NODE,
		    (cur->name != NULL) ? (char*)cur->name : "NULL");
	return(-1);
    }
*/        
    return(0);
}

/**
 * xmlSecCipherDataNodeRead:
 */
static int
xmlSecCipherDataNodeRead(xmlNodePtr cipherDataNode, xmlSecEncStatePtr state, 
			 xmlSecEncResultPtr result) {
    xmlNodePtr cur;
    int ret;

    xmlSecAssert2(cipherDataNode != NULL, -1);
    xmlSecAssert2(state!= NULL, -1);    
    xmlSecAssert2(result != NULL, -1);
    
    cur = xmlSecGetNextElementNode(cipherDataNode->children);

    /* CipherValue or CipherReference */
    if((cur != NULL) && (xmlSecCheckNodeName(cur, BAD_CAST "CipherValue", xmlSecEncNs))) {
	ret = xmlSecCipherValueNodeRead(cur, state, result);
	if(ret < 0){
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"xmlSecCipherValueNodeRead - %d", ret);
	    return(-1);
	}
	cur = xmlSecGetNextElementNode(cur->next);	
    } else if((cur != NULL) && (xmlSecCheckNodeName(cur, BAD_CAST "CipherReference",  xmlSecEncNs))) { 
	ret = xmlSecCipherReferenceNodeRead(cur, state, result);
	if(ret < 0){
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"xmlSecCipherReferenceNodeRead - %d", ret);
	    return(-1);
	}
	cur = xmlSecGetNextElementNode(cur->next);	
    }

    if(cur != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_NODE,
		    (cur->name != NULL) ? (char*)cur->name : "NULL");
	return(-1);
    }
    return(0);
}


/**
 * xmlSecCipherDataNodeWrite:
 */
static int
xmlSecCipherDataNodeWrite(xmlNodePtr cipherDataNode,
		      const unsigned char *buf, size_t size) {
    xmlNodePtr cur; 

    xmlSecAssert2(cipherDataNode != NULL, -1);
    xmlSecAssert2(buf != NULL, -1);    

    cur = xmlSecGetNextElementNode(cipherDataNode->children);
    if(cur == NULL) {
	cur = xmlSecAddChild(cipherDataNode, BAD_CAST "CipherValue", xmlSecEncNs);
	if(cur == NULL) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"xmlSecAddChild(CipherValue)");
	    return(-1);	    	    
	}
        xmlNodeSetContent(cur, BAD_CAST "\n");
	xmlNodeAddContentLen(cur, buf, size);
        xmlNodeAddContent(cur, BAD_CAST "\n");
	return(0);
    } else if(xmlSecCheckNodeName(cur, BAD_CAST "CipherValue", xmlSecEncNs)) {
        xmlNodeSetContent(cur, BAD_CAST "\n");
	xmlNodeAddContentLen(cur, buf, size);
        xmlNodeAddContent(cur, BAD_CAST "\n");
	cur = xmlSecGetNextElementNode(cur->next);
    } else if(xmlSecCheckNodeName(cur, BAD_CAST "CipherReference", xmlSecEncNs)) {
	/* do nothing! */
	cur = xmlSecGetNextElementNode(cur->next);
    }
    if(cur != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_NODE,
		    (cur->name != NULL) ? (char*)cur->name : "NULL");
	return(-1);
    }
    return(0);
}

/**
 * xmlSecCipherValueNodeRead:
 */
static int
xmlSecCipherValueNodeRead(xmlNodePtr cipherValueNode, xmlSecEncStatePtr state, 
			  xmlSecEncResultPtr result) {
    xmlSecTransformPtr base64;
    xmlSecTransformPtr memBuf;
    xmlChar *content;
    int ret;

    xmlSecAssert2(cipherValueNode != NULL, -1);
    xmlSecAssert2(state!= NULL, -1);    
    xmlSecAssert2(result != NULL, -1);
    
    /* first transform for decryption is base64 decode */	
    base64 = xmlSecTransformCreate(xmlSecEncBase64Decode, 0, 0);
    if(base64 == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecTransformCreate(xmlSecEncBase64Decode)");
	return(-1);
    }

    ret = xmlSecEncStateAddFirstTransform(state, base64);
    if(ret < 0) {    
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecEncStateAddFirstTransform(xmlSecEncBase64Decode) - %d", ret);
	xmlSecTransformDestroy(base64, 1); 
	return(-1);
    }


    /* add mem buf at the end */
    memBuf = xmlSecTransformCreate(xmlSecMemBuf, 0, 0);
    if(memBuf == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecTransformCreate(xmlSecMemBuf)");
	return(-1);
    }
    ret = xmlSecEncStateAddTransform(state, memBuf);
    if(ret < 0) {    
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecEncStateAddFirstTransform(xmlSecMemBuf) - %d", ret);
	xmlSecTransformDestroy(memBuf, 1); 
	return(-1);
    }


    /* get node content */
    content = xmlNodeGetContent(cipherValueNode);
    if(content == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_NODE_CONTENT,
		    "xmlNodeGetContent(cipherValueNode)");
	return(-1);
    }
	
    /* decrypt the data */
    ret = xmlSecBinTransformWrite((xmlSecTransformPtr)state->first, 
				  content, xmlStrlen(content));
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecBinTransformWrite - %d", ret);
	xmlFree(content);
	return(-1);	    	
    }
	
    /* flush everything */
    ret = xmlSecBinTransformFlush((xmlSecTransformPtr)state->first);
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecBinTransformWFlush - %d", ret);
	xmlFree(content);
        return(-1);	    	
    }

    result->buffer = xmlSecMemBufTransformGetBuffer((xmlSecTransformPtr)state->last, 1);
    if(result->buffer == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecMemBufTransformGetBuffer");
	xmlFree(content);
        return(-1);	    	
    }
    
    xmlFree(content);
    return(0);
}

static int			
xmlSecCipherReferenceNodeRead(xmlNodePtr cipherReferenceNode, xmlSecEncStatePtr state, 
			      xmlSecEncResultPtr result) {
    xmlSecBinTransformPtr transform;
    xmlSecTransformStatePtr transState = NULL;
    xmlNodePtr cur;
    xmlChar *uri = NULL;
    int res = -1;
    int ret;

    xmlSecAssert2(cipherReferenceNode != NULL, -1);
    xmlSecAssert2(state!= NULL, -1);    
    xmlSecAssert2(result != NULL, -1);
    
    cur = xmlSecGetNextElementNode(cipherReferenceNode->children);     
    uri = xmlGetProp(cipherReferenceNode, BAD_CAST "URI");
    transState = xmlSecTransformStateCreate(cipherReferenceNode->doc, NULL, (char*)uri);
    if(transState == NULL){
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecTransformStateCreate");
	goto done;
    }	
    
    /* first is optional Transforms node */
    if((cur != NULL) && xmlSecCheckNodeName(cur, BAD_CAST "Transforms", xmlSecEncNs)) {
	ret = xmlSecTransformsNodeRead(transState, cur);
	if(ret < 0){
	    xmlSecError(XMLSEC_ERRORS_HERE,
		        XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"xmlSecTransformsNodeRead - %d", ret);
	    goto done;
	}	
	cur = xmlSecGetNextElementNode(cur->next);
    }
    if(cur != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_NODE,
		    (cur->name != NULL) ? (char*)cur->name : "NULL");
	goto done;
    }

    /* copy all transforms from encryption state to transform state */
    while(state->first != NULL) {
	transform = state->first;
	state->first = state->first->next;
	transform->next = NULL;
	if(state->first != NULL) {
	    state->first->prev = NULL;
	}
	
	ret = xmlSecTransformStateUpdate(transState, (xmlSecTransformPtr)transform);
	if(ret < 0) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
		        "xmlSecTransformStateUpdate - %d", ret);
	    xmlSecBinTransformDestroyAll((xmlSecTransformPtr)transform);
	    goto done;
	}
    }
    state->last = NULL;
    ret = xmlSecTransformStateFinal(transState, xmlSecTransformResultBinary);
    if((ret < 0) || (transState->curBuf == NULL)){
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecTransformStateFinal - %d", ret);
	goto done;
    }
    result->buffer = transState->curBuf;
    transState->curBuf = NULL;
    
    res = 0;
done:
    if(uri != NULL) {
	xmlFree(uri);
    }
    if(transState != NULL) {
	xmlSecTransformStateDestroy(transState);
    }
    return(res);
}

#endif /* XMLSEC_NO_XMLENC */

