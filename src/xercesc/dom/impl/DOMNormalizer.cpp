/*
 * The Apache Software License, Version 1.1
 *
 * Copyright (c) 2001-2003 The Apache Software Foundation.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The end-user documentation included with the redistribution,
 *    if any, must include the following acknowledgment:
 *       "This product includes software developed by the
 *        Apache Software Foundation (http://www.apache.org/)."
 *    Alternately, this acknowledgment may appear in the software itself,
 *    if and wherever such third-party acknowledgments normally appear.
 *
 * 4. The names "Xerces" and "Apache Software Foundation" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact apache\@apache.org.
 *
 * 5. Products derived from this software may not be called "Apache",
 *    nor may "Apache" appear in their name, without prior written
 *    permission of the Apache Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE APACHE SOFTWARE FOUNDATION OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Software Foundation, and was
 * originally based on software copyright (c) 2001, International
 * Business Machines, Inc., http://www.ibm.com .  For more information
 * on the Apache Software Foundation, please see
 * <http://www.apache.org/>.
 */

#include <xercesc/dom/DOMAttr.hpp>
#include <xercesc/dom/DOMNode.hpp>
#include <xercesc/dom/DOMErrorHandler.hpp>
#include <xercesc/dom/DOMError.hpp>
#include <xercesc/dom/DOMText.hpp>
#include <xercesc/framework/XMLBuffer.hpp>

#include <xercesc/util/Mutexes.hpp>
#include <xercesc/util/PlatformUtils.hpp>
#include <xercesc/util/XMLMsgLoader.hpp>
#include <xercesc/util/XMLRegisterCleanup.hpp>
#include <xercesc/util/XMLString.hpp>
#include <xercesc/util/XMLUni.hpp>
#include <xercesc/util/XMLUniDefs.hpp>

#include "DOMConfigurationImpl.hpp"
#include "DOMDocumentImpl.hpp"
#include "DOMElementImpl.hpp"
#include "DOMErrorImpl.hpp"
#include "DOMEntityReferenceImpl.hpp"
#include "DOMLocatorImpl.hpp"
#include "DOMNormalizer.hpp"
#include "DOMTextImpl.hpp"

XERCES_CPP_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
//  Local static data
// ---------------------------------------------------------------------------

static bool            sRegistered = false;

static XMLMutex*       sNormalizerMutex = 0;
static XMLRegisterCleanup normalizerMutexCleanup;

static XMLMsgLoader*   gMsgLoader = 0;
static XMLRegisterCleanup cleanupMsgLoader;


// ---------------------------------------------------------------------------
//  Local, static functions
// ---------------------------------------------------------------------------

//  Cleanup for the message loader
void DOMNormalizer::reinitMsgLoader()
{
	delete gMsgLoader;
	gMsgLoader = 0;
}

//  Cleanup for the normalizer mutex
void DOMNormalizer::reinitNormalizerMutex()
{
    delete sNormalizerMutex;
    sNormalizerMutex = 0;
    sRegistered = false;
}

//
//  We need to fault in this mutex. But, since its used for synchronization
//  itself, we have to do this the low level way using a compare and swap.
//
static XMLMutex& gNormalizerMutex()
{

    if (!sNormalizerMutex)
    {
        XMLMutex* tmpMutex = new XMLMutex;
        if (XMLPlatformUtils::compareAndSwap((void**)&sNormalizerMutex, tmpMutex, 0))
        {
            // Someone beat us to it, so let's clean up ours
            delete tmpMutex;
        }

        // Now lock it and try to register it
        XMLMutexLock lock(sNormalizerMutex);

        // If we got here first, then register it and set the registered flag
        if (!sRegistered)
        {
			normalizerMutexCleanup.registerCleanup(DOMNormalizer::reinitNormalizerMutex);
            sRegistered = true;
        }
    }
    return *sNormalizerMutex;
}

static XMLMsgLoader& gNormalizerMsgLoader()
{
    XMLMutexLock lockInit(&gNormalizerMutex());

    // If we haven't loaded our message yet, then do that
    if (!gMsgLoader)
    {
        gMsgLoader = XMLPlatformUtils::loadMsgSet(XMLUni::fgXMLErrDomain);
        if (!gMsgLoader)
            XMLPlatformUtils::panic(PanicHandler::Panic_CantLoadMsgDomain);

        // Register this object to be cleaned up at termination
        cleanupMsgLoader.registerCleanup(DOMNormalizer::reinitMsgLoader);
    }

    return *gMsgLoader;
}


DOMNormalizer::DOMNormalizer() 
    : fDocument(0)
    , fNewNamespaceCount(1)
    , fMemoryManager(XMLPlatformUtils::fgMemoryManager)
{
    fNSScope = new InScopeNamespaces();
};

DOMNormalizer::~DOMNormalizer() {
    delete fNSScope;
}

void DOMNormalizer::normalizeDocument(DOMDocumentImpl *doc) {
 
    fDocument = doc;
    fConfiguration = (DOMConfigurationImpl*)doc->getDOMConfiguration();
    DOMConfigurationImpl *dci = (DOMConfigurationImpl*)fDocument->getDOMConfiguration();
    if(dci)
        fErrorHandler = dci->getErrorHandler();            
    else 
        fErrorHandler = 0;

    DOMNode *child = 0;
    DOMNode *next = 0;
    ((DOMNormalizer *)this)->fNewNamespaceCount = 1;

    for(child = doc->getFirstChild();child != 0; child = next) {
        next = child->getNextSibling();
        child = normalizeNode(child);
        if(child != 0) {
            next = child;
        }
    }
}

DOMNode * DOMNormalizer::normalizeNode(DOMNode *node) const {
    switch(node->getNodeType()) {
    case DOMNode::ELEMENT_NODE: {
        fNSScope->addScope();
        DOMNamedNodeMap *attrMap = node->getAttributes();

        if(fConfiguration->featureValues & DOMConfigurationImpl::FEATURE_NAMESPACES) {
            namespaceFixUp((DOMElementImpl*)node);
        }
        else {
            //this is done in namespace fixup so no need to do it if namespace is on 
            if(attrMap) {
                for(XMLSize_t i = 0; i < attrMap->getLength(); i++) {
                    attrMap->item(i)->normalize();
                }
            }
        }

        DOMNode *child = node->getFirstChild();
        DOMNode *next = 0;
        for (; child != 0; child = next) {
            next = child->getNextSibling();
            child = normalizeNode(child);
            if(child != 0) {
                next = child;
            }
        }
        fNSScope->removeScope();
        break;
    }
    case DOMNode::COMMENT_NODE: {  
        if (!(fConfiguration->featureValues & DOMConfigurationImpl::FEATURE_COMMENTS)) {
            DOMNode *prevSibling = node->getPreviousSibling();
            DOMNode *parent = node->getParentNode();
            // remove the comment node
            parent->removeChild(node);
            if (prevSibling != 0 && prevSibling->getNodeType() == DOMNode::TEXT_NODE) {
                DOMNode *nextSibling = prevSibling->getNextSibling();
                if (nextSibling != 0 && nextSibling->getNodeType() == DOMNode::TEXT_NODE) {
                    ((DOMTextImpl*)nextSibling)->insertData(0, prevSibling->getNodeValue());
                    parent->removeChild(prevSibling);
                    return nextSibling;
                }
            }
        }
        break;
    }
    case DOMNode::CDATA_SECTION_NODE: {
        if (!(fConfiguration->featureValues & DOMConfigurationImpl::FEATURE_CDATA_SECTIONS)) {
            // convert CDATA to TEXT nodes
            DOMText *text = fDocument->createTextNode(node->getNodeValue());
            DOMNode *parent = node->getParentNode();
            DOMNode *prevSibling = node->getPreviousSibling();
            node = parent->replaceChild(text, node);
            if (prevSibling != 0 && prevSibling->getNodeType() == DOMNode::TEXT_NODE) {
                text->insertData(0, prevSibling->getNodeValue());
                parent->removeChild(prevSibling);
            }
            return text; // Don't advance; 
        }
        break;
    }
    case DOMNode::TEXT_NODE: {
        DOMNode *next = node->getNextSibling();
        
        if(next != 0 && next->getNodeType() == DOMNode::TEXT_NODE) {
            ((DOMText*)node)->appendData(next->getNodeValue());
            node->getParentNode()->removeChild(next);
            return node;
        } else if (XMLString::stringLen(node->getNodeValue()) == 0) {
            node->getParentNode()->removeChild(node);
        }

    }
    }

    return 0;
}


void DOMNormalizer::namespaceFixUp(DOMElementImpl *ele) const {
    DOMAttrMapImpl *attrMap = ele->fAttributes;

    int len = attrMap->getLength();
    //get the ns info from the attrs
    for(int i = 0; i < len; i++) {
        DOMAttr *at = (DOMAttr*)attrMap->item(i);

        //normalize the attr whatever happens
        at->normalize();

        const XMLCh *uri = at->getNamespaceURI();
        const XMLCh *value = at->getNodeValue();
            
        if(XMLString::equals(XMLUni::fgXMLNSURIName, uri)) {
            if(XMLString::equals(XMLUni::fgXMLNSURIName, value)) {
                error(XMLErrs::NSDeclInvalid, ele);
            }
            else {
                const XMLCh *prefix = at->getPrefix();
                
                if(XMLString::equals(prefix, XMLUni::fgXMLNSString)) {
                    fNSScope->addOrChangeBinding(at->getLocalName(), value);
                }
                else {
                    fNSScope->addOrChangeBinding(XMLUni::fgZeroLenString, value);
                }
            }
        }
    }

    const XMLCh* prefix = ele->getPrefix();
    prefix ? prefix : prefix = XMLUni::fgZeroLenString;
    const XMLCh* uri = ele->getNamespaceURI();
    uri ? uri : uri = XMLUni::fgZeroLenString;

    if(!XMLString::equals(uri, XMLUni::fgZeroLenString)) {
        if(!fNSScope->isValidBinding(prefix, uri)) {
            addOrChangeNamespaceDecl(prefix, uri, ele);
            fNSScope->addOrChangeBinding(prefix, uri);
        }
    }
    else {
        if(ele->getLocalName() == 0) {
            error(XMLErrs::DOMLevel1Node, ele);
        }
        else if(!fNSScope->isValidBinding(XMLUni::fgZeroLenString, XMLUni::fgZeroLenString)) {
            addOrChangeNamespaceDecl(XMLUni::fgZeroLenString, XMLUni::fgZeroLenString, ele);
            fNSScope->addOrChangeBinding(XMLUni::fgZeroLenString, XMLUni::fgZeroLenString);
        }
    }

    //fix up non ns attrs
    len = attrMap->getLength();

    for(int i = 0; i < len; i++) {
        DOMAttr *at = (DOMAttr*)attrMap->item(i);
        const XMLCh *uri = at->getNamespaceURI();
        const XMLCh *value = at->getNodeValue();
        const XMLCh* prefix = at->getPrefix();

        if(!XMLString::equals(XMLUni::fgXMLNSURIName, uri)) {
            if(uri != 0) {
                if(prefix == 0 || !fNSScope->isValidBinding(prefix, uri)) {
                    
                    const XMLCh* newPrefix =  fNSScope->getPrefix(uri);

                    if(newPrefix != 0) {
                        at->setPrefix(newPrefix);                                                
                    }
                    else {
                        if(prefix != 0 && !fNSScope->getUri(prefix)) {
                            fNSScope->addOrChangeBinding(prefix, uri);
                            addOrChangeNamespaceDecl(prefix, uri, ele);
                        }
                        else {
                            newPrefix = addCustomNamespaceDecl(uri, ele);
                            fNSScope->addOrChangeBinding(newPrefix, uri);
                            at->setPrefix(newPrefix);
                        }
                    }
                }
            }
            else if(at->getLocalName() == 0) {
                error(XMLErrs::DOMLevel1Node, at);
            }
        }
    }
}



const XMLCh * DOMNormalizer::integerToXMLCh(unsigned int i) const {
    XMLCh *buf = new XMLCh[15];
	XMLCh *pos = buf + sizeof(buf) - sizeof(XMLCh);
	*pos = chNull;

	do {
        switch(i % 10) {
        case 0 : *--pos = chDigit_0;break;
        case 1 : *--pos = chDigit_1;break;
        case 2 : *--pos = chDigit_2;break;
        case 3 : *--pos = chDigit_3;break;
        case 4 : *--pos = chDigit_4;break;
        case 5 : *--pos = chDigit_5;break;
        case 6 : *--pos = chDigit_6;break;
        case 7 : *--pos = chDigit_7;break;
        case 8 : *--pos = chDigit_8;break;
        case 9 : *--pos = chDigit_9;break;
        default:;
        }
		i /= 10;
	} while (i);

    const XMLCh *copy = fDocument->getPooledString(pos);
    delete[] buf;
	return copy;
}





void DOMNormalizer::addOrChangeNamespaceDecl(const XMLCh* prefix, const XMLCh* uri, DOMElementImpl* element) const {

    if (XMLString::equals(prefix, XMLUni::fgZeroLenString)) {
        element->setAttributeNS(XMLUni::fgXMLNSURIName, XMLUni::fgXMLNSString, uri);
    } else {
        XMLBuffer buf(1023, fMemoryManager);
        buf.set(XMLUni::fgXMLNSString);
        buf.append(chColon);
        buf.append(prefix);
        element->setAttributeNS(XMLUni::fgXMLNSURIName, buf.getRawBuffer(), uri); 
    }
}

const XMLCh* DOMNormalizer::addCustomNamespaceDecl(const XMLCh* uri, DOMElementImpl *element) const {
    XMLBuffer preBuf(1023, fMemoryManager);
    preBuf.append(chLatin_N);
    preBuf.append(chLatin_S);
    preBuf.append(integerToXMLCh(fNewNamespaceCount));
    ((DOMNormalizer *)this)->fNewNamespaceCount++;

    while(fNSScope->getUri(preBuf.getRawBuffer())) {
        preBuf.reset();
        preBuf.append(chLatin_N);
        preBuf.append(chLatin_S);
        preBuf.append(integerToXMLCh(fNewNamespaceCount));
        ((DOMNormalizer *)this)->fNewNamespaceCount++;
    }
    
    XMLBuffer buf(1023, fMemoryManager);
    buf.set(XMLUni::fgXMLNSString);
    buf.append(chColon);
    buf.append(preBuf.getRawBuffer());
    element->setAttributeNS(XMLUni::fgXMLNSURIName, buf.getRawBuffer(), uri);

    return element->getAttributeNodeNS(XMLUni::fgXMLNSURIName, preBuf.getRawBuffer())->getLocalName();
}

int DOMNormalizer::InScopeNamespaces::size() {
    return fScopes->size();
}

DOMNormalizer::InScopeNamespaces::InScopeNamespaces() : lastScopeWithBindings(0) {
    fScopes = new RefVectorOf<Scope>(10);
}

DOMNormalizer::InScopeNamespaces::~InScopeNamespaces() {
    delete fScopes;
}

void DOMNormalizer::InScopeNamespaces::addOrChangeBinding(const XMLCh *prefix, const XMLCh *uri) {
    unsigned int s = fScopes->size();

    if(!s)
        addScope();
    
    Scope *curScope = fScopes->elementAt(s - 1);
    curScope->addOrChangeBinding(prefix, uri);

    lastScopeWithBindings = curScope;
}

void DOMNormalizer::InScopeNamespaces::addScope() {
    Scope *s = new Scope(lastScopeWithBindings);
    fScopes->addElement(s);
}

void DOMNormalizer::InScopeNamespaces::removeScope() {
    lastScopeWithBindings = fScopes->elementAt(fScopes->size() - 1)->fBaseScopeWithBindings;
    Scope *s = fScopes->orphanElementAt(fScopes->size() - 1);
    delete s;
}

bool DOMNormalizer::InScopeNamespaces::isValidBinding(const XMLCh* prefix, const XMLCh* uri) const {
    const XMLCh* actual = fScopes->elementAt(fScopes->size() - 1)->getUri(prefix);
    if(actual == 0 || !XMLString::equals(actual, uri))
        return false;
    return true;
}

const XMLCh* DOMNormalizer::InScopeNamespaces::getPrefix(const XMLCh* uri) const {
    return fScopes->elementAt(fScopes->size() - 1)->getPrefix(uri);
}

const XMLCh* DOMNormalizer::InScopeNamespaces::getUri(const XMLCh* prefix) const {
    return fScopes->elementAt(fScopes->size() - 1)->getUri(prefix);
}



DOMNormalizer::InScopeNamespaces::Scope::Scope(Scope *baseScopeWithBindings) : fBaseScopeWithBindings(baseScopeWithBindings), fPrefixHash(0), fUriHash(0)
{
}

DOMNormalizer::InScopeNamespaces::Scope::~Scope() {
    delete fPrefixHash;
    delete fUriHash;
}

void DOMNormalizer::InScopeNamespaces::Scope::addOrChangeBinding(const XMLCh *prefix, const XMLCh *uri) {
    //initialize and copy forward now we need to
    if(!fUriHash) {
        fPrefixHash = new RefHashTableOf<XMLCh>(10, (bool) false);
        fUriHash = new RefHashTableOf<XMLCh>(10, (bool) false);
        
        if(fBaseScopeWithBindings) {
            RefHashTableOfEnumerator<XMLCh> preEnumer(fBaseScopeWithBindings->fPrefixHash);
            while(preEnumer.hasMoreElements()) {
                const XMLCh* prefix = (XMLCh*) preEnumer.nextElementKey();
                const XMLCh* uri  = fBaseScopeWithBindings->fPrefixHash->get((void*)prefix);
                
                //have to cast here because otherwise we have delete problems under windows :(
                fPrefixHash->put((void *)prefix, (XMLCh*)uri);
            }
            
            RefHashTableOfEnumerator<XMLCh> uriEnumer(fBaseScopeWithBindings->fUriHash);
            while(uriEnumer.hasMoreElements()) {
                const XMLCh* uri = (XMLCh*) uriEnumer.nextElementKey();
                const XMLCh* prefix  = fBaseScopeWithBindings->fUriHash->get((void*)uri);

                //have to cast here because otherwise we have delete problems under windows :(
                fUriHash->put((void *)uri, (XMLCh*)prefix);
            }
        }
    }

    const XMLCh *oldUri = fPrefixHash->get(prefix);
    if(oldUri) {
        fUriHash->removeKey(oldUri);
    }

    fPrefixHash->put((void *)prefix, (XMLCh*)uri);
    fUriHash->put((void *)uri, (XMLCh*)prefix);
}

const XMLCh* DOMNormalizer::InScopeNamespaces::Scope::getUri(const XMLCh *prefix) const {
    const XMLCh* uri = 0;

    if(fPrefixHash) {
        uri = fPrefixHash->get(prefix);
    }
    else if(fBaseScopeWithBindings) {
        uri = fBaseScopeWithBindings->getUri(prefix);
    }
    
    return uri ? uri : 0;
}

const XMLCh* DOMNormalizer::InScopeNamespaces::Scope::getPrefix(const XMLCh* uri) const {
    const XMLCh* prefix = 0;

    if(fUriHash) {
        prefix = fUriHash->get(uri);
    }
    else if(fBaseScopeWithBindings) {
        prefix = fBaseScopeWithBindings->getPrefix(uri);
    }
    return prefix ? prefix : 0;
}

void DOMNormalizer::error(const XMLErrs::Codes code, const DOMNode *node) const
{
    if (fErrorHandler) {

        //  Load the message into alocal and replace any tokens found in
        //  the text.
        const unsigned int maxChars = 2047;
        XMLCh errText[maxChars + 1];

        if (!gNormalizerMsgLoader().loadMsg(code, errText, maxChars))
        {
                // <TBD> Should probably load a default message here
        }

        DOMErrorImpl domError(code, 0, errText, (void*)node);
        if (!fErrorHandler->handleError(domError))
            throw (XMLErrs::Codes) code;
    }
}



XERCES_CPP_NAMESPACE_END
