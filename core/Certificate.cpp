/*
 * Copyright (c)2013-2021 ZeroTier, Inc.
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file in the project's root directory.
 *
 * Change Date: 2026-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2.0 of the Apache License.
 */
/****/

#include "Certificate.hpp"
#include "SHA512.hpp"
#include "ECC384.hpp"
#include "ScopedPtr.hpp"

namespace ZeroTier {

Certificate::Certificate() noexcept
{
	ZT_Certificate *const sup = this;
	Utils::zero< sizeof(ZT_Certificate) >(sup);
}

Certificate::Certificate(const ZT_Certificate &apiCert) :
	Certificate()
{ *this = apiCert; }

Certificate::Certificate(const Certificate &cert) :
	Certificate()
{ *this = cert; }

Certificate::~Certificate()
{}

Certificate &Certificate::operator=(const ZT_Certificate &cert)
{
	m_clear();

	Utils::copy< 48 >(this->serialNo, cert.serialNo);
	this->flags = cert.flags;
	this->timestamp = cert.timestamp;
	this->validity[0] = cert.validity[0];
	this->validity[1] = cert.validity[1];

	this->subject.timestamp = cert.subject.timestamp;

	if (cert.subject.identities) {
		for (unsigned int i = 0; i < cert.subject.identityCount; ++i) {
			if (cert.subject.identities[i].identity) {
				if (cert.subject.identities[i].locator) {
					addSubjectIdentity(*reinterpret_cast<const Identity *>(cert.subject.identities[i].identity), *reinterpret_cast<const Locator *>(cert.subject.identities[i].locator));
				} else {
					addSubjectIdentity(*reinterpret_cast<const Identity *>(cert.subject.identities[i].identity));
				}
			}
		}
	}

	if (cert.subject.networks) {
		for (unsigned int i = 0; i < cert.subject.networkCount; ++i) {
			if (cert.subject.networks[i].id)
				addSubjectNetwork(cert.subject.networks[i].id, cert.subject.networks[i].controller);
		}
	}

	if (cert.subject.certificates) {
		for (unsigned int i = 0; i < cert.subject.certificateCount; ++i) {
			if (cert.subject.certificates[i])
				addSubjectCertificate(cert.subject.certificates[i]);
		}
	}

	if (cert.subject.updateURLs) {
		for (unsigned int i = 0; i < cert.subject.updateURLCount; ++i) {
			if (cert.subject.updateURLs[i])
				addSubjectUpdateUrl(cert.subject.updateURLs[i]);
		}
	}

	Utils::copy< sizeof(ZT_Certificate_Name) >(&(this->subject.name), &(cert.subject.name));

	if ((cert.subject.uniqueId) && (cert.subject.uniqueIdSize > 0)) {
		m_subjectUniqueId.assign(cert.subject.uniqueId, cert.subject.uniqueId + cert.subject.uniqueIdSize);
		this->subject.uniqueId = m_subjectUniqueId.data();
		this->subject.uniqueIdSize = (unsigned int)m_subjectUniqueId.size();
	}
	if ((cert.subject.uniqueIdProofSignature) && (cert.subject.uniqueIdProofSignatureSize > 0)) {
		m_subjectUniqueIdProofSignature.assign(cert.subject.uniqueIdProofSignature, cert.subject.uniqueIdProofSignature + cert.subject.uniqueIdProofSignatureSize);
		this->subject.uniqueIdProofSignature = m_subjectUniqueIdProofSignature.data();
		this->subject.uniqueIdProofSignatureSize = (unsigned int)m_subjectUniqueIdProofSignature.size();
	}

	if (cert.issuer) {
		m_identities.push_front(*reinterpret_cast<const Identity *>(cert.issuer));
		this->issuer = &(m_identities.front());
	}

	Utils::copy< sizeof(ZT_Certificate_Name) >(&(this->issuerName), &(cert.issuerName));

	if ((cert.extendedAttributes) && (cert.extendedAttributesSize > 0)) {
		m_extendedAttributes.assign(cert.extendedAttributes, cert.extendedAttributes + cert.extendedAttributesSize);
		this->extendedAttributes = m_extendedAttributes.data();
		this->extendedAttributesSize = (unsigned int)m_extendedAttributes.size();
	}

	this->maxPathLength = cert.maxPathLength;

	if ((cert.signature) && (cert.signatureSize > 0)) {
		m_signature.assign(cert.signature, cert.signature + cert.signatureSize);
		this->signature = m_signature.data();
		this->signatureSize = (unsigned int)m_signature.size();
	}

	return *this;
}

ZT_Certificate_Identity *Certificate::addSubjectIdentity(const Identity &id)
{
	// Store a local copy of the actual identity.
	m_identities.push_front(id);
	m_identities.front().erasePrivateKey();

	// Enlarge array of ZT_Certificate_Identity structs and set pointer to potentially reallocated array.
	m_subjectIdentities.push_back(ZT_Certificate_Identity());
	m_subjectIdentities.back().identity = &(m_identities.front());
	m_subjectIdentities.back().locator = nullptr;
	this->subject.identities = m_subjectIdentities.data();
	this->subject.identityCount = (unsigned int)m_subjectIdentities.size();

	return &(m_subjectIdentities.back());
}

ZT_Certificate_Identity *Certificate::addSubjectIdentity(const Identity &id, const Locator &loc)
{
	// Add identity as above.
	ZT_Certificate_Identity *const n = addSubjectIdentity(id);

	// Store local copy of locator.
	m_locators.push_front(loc);

	// Set pointer to stored local copy of locator.
	n->locator = &(m_locators.front());

	return n;
}

ZT_Certificate_Network *Certificate::addSubjectNetwork(const uint64_t id, const ZT_Fingerprint &controller)
{
	// Enlarge array of ZT_Certificate_Network and set pointer to potentially reallocated array.
	m_subjectNetworks.resize(++this->subject.networkCount);
	this->subject.networks = m_subjectNetworks.data();

	// Set fields in new ZT_Certificate_Network structure.
	m_subjectNetworks.back().id = id;
	Utils::copy< sizeof(ZT_Fingerprint) >(&(m_subjectNetworks.back().controller), &controller);

	return &(m_subjectNetworks.back());
}

void Certificate::addSubjectCertificate(const uint8_t serialNo[ZT_SHA384_DIGEST_SIZE])
{
	// Store local copy of serial in m_serials container.
	m_serials.push_front(H384(serialNo));

	// Enlarge array of uint8_t pointers, set new pointer to local copy of serial, and set
	// certificates to point to potentially reallocated array.
	m_subjectCertificates.push_back(m_serials.front().bytes());
	this->subject.certificates = m_subjectCertificates.data();
	this->subject.certificateCount = (unsigned int)m_subjectCertificates.size();
}

void Certificate::addSubjectUpdateUrl(const char *url)
{
	if ((!url) || (!url[0]))
		return;

	// Store local copy of URL.
	m_strings.push_front(url);

	// Add pointer to local copy to pointer array and update C structure to point to
	// potentially reallocated array.
	m_updateUrls.push_back(m_strings.front().c_str());
	this->subject.updateURLs = m_updateUrls.data();
	this->subject.updateURLCount = (unsigned int)m_updateUrls.size();
}

void Certificate::setExtendedAttributes(const Dictionary &x)
{
	m_extendedAttributes.clear();
	x.encode(m_extendedAttributes);
	this->extendedAttributes = m_extendedAttributes.data();
	this->extendedAttributesSize = (unsigned int)m_extendedAttributes.size();
}

bool Certificate::setSubjectUniqueId(const uint8_t uniqueId[ZT_CERTIFICATE_UNIQUE_ID_TYPE_NIST_P_384_SIZE], const uint8_t uniqueIdPrivate[ZT_CERTIFICATE_UNIQUE_ID_TYPE_NIST_P_384_PRIVATE_SIZE])
{
	m_subjectUniqueId.assign(uniqueId, uniqueId + ZT_CERTIFICATE_UNIQUE_ID_TYPE_NIST_P_384_SIZE);
	this->subject.uniqueId = m_subjectUniqueId.data();
	this->subject.uniqueIdSize = ZT_CERTIFICATE_UNIQUE_ID_TYPE_NIST_P_384_SIZE;

	Dictionary d;
	m_encodeSubject(this->subject, d, true);
	Vector< uint8_t > enc;
	d.encode(enc);
	uint8_t h[ZT_SHA384_DIGEST_SIZE];
	SHA384(h, enc.data(), (unsigned int)enc.size());

	m_subjectUniqueIdProofSignature.resize(ZT_ECC384_SIGNATURE_SIZE);
	ECC384ECDSASign(uniqueIdPrivate, h, m_subjectUniqueIdProofSignature.data());

	this->subject.uniqueIdProofSignature = m_subjectUniqueIdProofSignature.data();
	this->subject.uniqueIdProofSignatureSize = ZT_ECC384_SIGNATURE_SIZE;

	return true;
}

Vector< uint8_t > Certificate::encode(const bool omitSignature) const
{
	Vector< uint8_t > enc;
	Dictionary d;

	/*
	 * A Dictionary is used to encode certificates as it's a common and extensible
	 * format. Custom packed formats are used for credentials as these are smaller
	 * and faster to marshal/unmarshal.
	 *
	 * We use the slower actually-insert-keys method of building a dictionary
	 * instead of the faster append method because for signing and verification
	 * purposes the keys must be always be in order.
	 */

	if (this->flags != 0)
		d.add("f", this->flags);
	if (this->timestamp > 0)
		d.add("t", (uint64_t)this->timestamp);
	if (this->validity[0] > 0)
		d.add("v#0", (uint64_t)this->validity[0]);
	if (this->validity[1] > 0)
		d.add("v#1", (uint64_t)this->validity[1]);
	if (this->maxPathLength > 0)
		d.add("mP", (uint64_t)this->maxPathLength);

	m_encodeSubject(this->subject, d, false);

	if (this->issuer)
		d.addO("i", *reinterpret_cast<const Identity *>(this->issuer));

	if (this->issuerName.country[0])
		d.add("iN.c", this->issuerName.country);
	if (this->issuerName.organization[0])
		d.add("iN.o", this->issuerName.organization);
	if (this->issuerName.unit[0])
		d.add("iN.u", this->issuerName.unit);
	if (this->issuerName.locality[0])
		d.add("iN.l", this->issuerName.locality);
	if (this->issuerName.province[0])
		d.add("iN.p", this->issuerName.province);
	if (this->issuerName.streetAddress[0])
		d.add("iN.sA", this->issuerName.streetAddress);
	if (this->issuerName.postalCode[0])
		d.add("iN.pC", this->issuerName.postalCode);
	if (this->issuerName.commonName[0])
		d.add("iN.cN", this->issuerName.commonName);
	if (this->issuerName.serialNo[0])
		d.add("iN.sN", this->issuerName.serialNo);
	if (this->issuerName.email[0])
		d.add("iN.e", this->issuerName.email);
	if (this->issuerName.url[0])
		d.add("iN.ur", this->issuerName.url);
	if (this->issuerName.host[0])
		d.add("iN.h", this->issuerName.host);

	if ((this->extendedAttributes) && (this->extendedAttributesSize > 0))
		d["x"].assign(this->extendedAttributes, this->extendedAttributes + this->extendedAttributesSize);

	if ((!omitSignature) && (this->signature) && (this->signatureSize > 0))
		d["S"].assign(this->signature, this->signature + this->signatureSize);

	d.encode(enc);
	return enc;
}

bool Certificate::decode(const void *const data, const unsigned int len)
{
	char tmp[32], tmp2[ZT_CERTIFICATE_MAX_STRING_LENGTH + 1];

	Dictionary d;
	if (!d.decode(data, len))
		return false;

	m_clear();

	this->flags = d.getUI("f");
	this->timestamp = (int64_t)d.getUI("t");
	this->validity[0] = (int64_t)d.getUI("v#0");
	this->validity[1] = (int64_t)d.getUI("v#1");
	this->maxPathLength = (unsigned int)d.getUI("mP");

	this->subject.timestamp = (int64_t)d.getUI("s.t");

	unsigned int cnt = (unsigned int)d.getUI("s.i$");
	for (unsigned int i = 0; i < cnt; ++i) {
		const Vector< uint8_t > &identityData = d[Dictionary::arraySubscript(tmp, sizeof(tmp), "s.i$.i", i)];
		const Vector< uint8_t > &locatorData = d[Dictionary::arraySubscript(tmp, sizeof(tmp), "s.i$.l", i)];
		if (identityData.empty())
			return false;
		Identity id;
		if (id.unmarshal(identityData.data(), (unsigned int)identityData.size()) <= 0)
			return false;
		if (locatorData.empty()) {
			this->addSubjectIdentity(id);
		} else {
			Locator loc;
			if (loc.unmarshal(locatorData.data(), (unsigned int)locatorData.size()) <= 0)
				return false;
			this->addSubjectIdentity(id, loc);
		}
	}

	cnt = (unsigned int)d.getUI("s.nw$");
	for (unsigned int i = 0; i < cnt; ++i) {
		const uint64_t nwid = d.getUI(Dictionary::arraySubscript(tmp, sizeof(tmp), "s.nw$.i", i));
		const Vector< uint8_t > &fingerprintData = d[Dictionary::arraySubscript(tmp, sizeof(tmp), "s.nw$.c", i)];
		if ((nwid == 0) || (fingerprintData.empty()))
			return false;
		Fingerprint fp;
		if (fp.unmarshal(fingerprintData.data(), (unsigned int)fingerprintData.size()) <= 0)
			return false;
		this->addSubjectNetwork(nwid, fp);
	}

	cnt = (unsigned int)d.getUI("s.c$");
	for (unsigned int i = 0; i < cnt; ++i) {
		const Vector< uint8_t > &serial = d[Dictionary::arraySubscript(tmp, sizeof(tmp), "s.c$", i)];
		if (serial.size() != ZT_SHA384_DIGEST_SIZE)
			return false;
		this->addSubjectCertificate(serial.data());
	}

	cnt = (unsigned int)d.getUI("s.u$");
	for (unsigned int i = 0; i < cnt; ++i)
		addSubjectUpdateUrl(d.getS(Dictionary::arraySubscript(tmp, sizeof(tmp), "s.u$", i), tmp2, sizeof(tmp2)));

	d.getS("s.n.sN", this->subject.name.serialNo, sizeof(this->subject.name.serialNo));
	d.getS("s.n.cN", this->subject.name.commonName, sizeof(this->subject.name.commonName));
	d.getS("s.n.c", this->subject.name.country, sizeof(this->subject.name.country));
	d.getS("s.n.o", this->subject.name.organization, sizeof(this->subject.name.organization));
	d.getS("s.n.u", this->subject.name.unit, sizeof(this->subject.name.unit));
	d.getS("s.n.l", this->subject.name.locality, sizeof(this->subject.name.locality));
	d.getS("s.n.p", this->subject.name.province, sizeof(this->subject.name.province));
	d.getS("s.n.sA", this->subject.name.streetAddress, sizeof(this->subject.name.streetAddress));
	d.getS("s.n.pC", this->subject.name.postalCode, sizeof(this->subject.name.postalCode));
	d.getS("s.n.e", this->subject.name.email, sizeof(this->subject.name.email));
	d.getS("s.n.ur", this->subject.name.url, sizeof(this->subject.name.url));
	d.getS("s.n.h", this->subject.name.host, sizeof(this->subject.name.host));

	m_subjectUniqueId = d["s.uI"];
	if (!m_subjectUniqueId.empty()) {
		this->subject.uniqueId = m_subjectUniqueId.data();
		this->subject.uniqueIdSize = (unsigned int)m_subjectUniqueId.size();
	}
	m_subjectUniqueIdProofSignature = d["s.uS"];
	if (!m_subjectUniqueIdProofSignature.empty()) {
		this->subject.uniqueIdProofSignature = m_subjectUniqueIdProofSignature.data();
		this->subject.uniqueIdProofSignatureSize = (unsigned int)m_subjectUniqueIdProofSignature.size();
	}

	const Vector< uint8_t > &issuerData = d["i"];
	if (!issuerData.empty()) {
		Identity id;
		if (id.unmarshal(issuerData.data(), (int)issuerData.size()) > 0) {
			m_identities.push_front(id);
			this->issuer = reinterpret_cast<const Identity *>(&(m_identities.front()));
		}
	}

	d.getS("iN.sN", this->issuerName.serialNo, sizeof(this->issuerName.serialNo));
	d.getS("iN.cN", this->issuerName.commonName, sizeof(this->issuerName.commonName));
	d.getS("iN.c", this->issuerName.country, sizeof(this->issuerName.country));
	d.getS("iN.o", this->issuerName.organization, sizeof(this->issuerName.organization));
	d.getS("iN.u", this->issuerName.unit, sizeof(this->issuerName.unit));
	d.getS("iN.l", this->issuerName.locality, sizeof(this->issuerName.locality));
	d.getS("iN.p", this->issuerName.province, sizeof(this->issuerName.province));
	d.getS("iN.sA", this->issuerName.streetAddress, sizeof(this->issuerName.streetAddress));
	d.getS("iN.pC", this->issuerName.postalCode, sizeof(this->issuerName.postalCode));
	d.getS("iN.e", this->issuerName.email, sizeof(this->issuerName.email));
	d.getS("iN.ur", this->issuerName.url, sizeof(this->issuerName.url));
	d.getS("iN.h", this->issuerName.host, sizeof(this->issuerName.host));

	cnt = (unsigned int)d.getUI("u$");
	for (unsigned int i = 0; i < cnt; ++i) {
		const char *const url = d.getS(Dictionary::arraySubscript(tmp, sizeof(tmp), "u$", i), tmp2, sizeof(tmp2));
		if ((url) && (*url != 0)) {
			addSubjectUpdateUrl(url);
		} else {
			return false;
		}
	}

	m_extendedAttributes = d["x"];
	if (!m_extendedAttributes.empty()) {
		this->extendedAttributes = m_extendedAttributes.data();
		this->extendedAttributesSize = (unsigned int)m_extendedAttributes.size();
	}

	m_signature = d["S"];
	if (!m_signature.empty()) {
		this->signature = m_signature.data();
		this->signatureSize = (unsigned int)m_signature.size();
	}

	const Vector< uint8_t > enc(encode(true));
	SHA384(this->serialNo, enc.data(), (unsigned int)enc.size());

	return true;
}

bool Certificate::sign(const Identity &issuer)
{
	m_identities.push_front(issuer);
	m_identities.front().erasePrivateKey();
	this->issuer = reinterpret_cast<const ZT_Identity *>(&(m_identities.front()));

	const Vector< uint8_t > enc(encode(true));
	SHA384(this->serialNo, enc.data(), (unsigned int)enc.size());

	uint8_t sig[ZT_SIGNATURE_BUFFER_SIZE];
	const unsigned int sigSize = issuer.sign(enc.data(), (unsigned int)enc.size(), sig, ZT_SIGNATURE_BUFFER_SIZE);

	if (sigSize > 0) {
		m_signature.assign(sig, sig + sigSize);
		this->signature = m_signature.data();
		this->signatureSize = sigSize;
		return true;
	}

	m_signature.clear();
	this->signature = nullptr;
	this->signatureSize = 0;
	return false;
}

ZT_CertificateError Certificate::verify(const int64_t clock, const bool checkSignatures) const
{
	try {
		if (this->validity[0] > this->validity[1]) {
			return ZT_CERTIFICATE_ERROR_INVALID_FORMAT;
		}
		if ((clock >= 0) && ((this->validity[0] > clock) || (this->validity[1] < clock))) {
			return ZT_CERTIFICATE_ERROR_OUT_OF_VALID_TIME_WINDOW;
		}

		if (this->issuer) {
			if (checkSignatures) {
				const Vector< uint8_t > enc(encode(true));
				if (!reinterpret_cast<const Identity *>(this->issuer)->verify(enc.data(), (unsigned int)enc.size(), this->signature, this->signatureSize))
					return ZT_CERTIFICATE_ERROR_INVALID_PRIMARY_SIGNATURE;
			}
		} else {
			return ZT_CERTIFICATE_ERROR_INVALID_PRIMARY_SIGNATURE;
		}

		if (this->subject.uniqueIdProofSignatureSize > 0) {
			if (
				(this->subject.uniqueIdProofSignatureSize != ZT_ECC384_SIGNATURE_SIZE) ||
				(this->subject.uniqueIdSize != ZT_CERTIFICATE_UNIQUE_ID_TYPE_NIST_P_384_SIZE) ||
				(this->subject.uniqueId[0] != ZT_CERTIFICATE_UNIQUE_ID_TYPE_NIST_P_384))
				return ZT_CERTIFICATE_ERROR_INVALID_UNIQUE_ID_PROOF;

			if (checkSignatures) {
				Dictionary d;
				m_encodeSubject(this->subject, d, true);
				Vector< uint8_t > enc;
				d.encode(enc);

				uint8_t h[ZT_SHA384_DIGEST_SIZE];
				SHA384(h, enc.data(), (unsigned int)enc.size());
				static_assert(ZT_CERTIFICATE_UNIQUE_ID_TYPE_NIST_P_384_SIZE == (ZT_ECC384_PUBLIC_KEY_SIZE + 1), "incorrect size");
				if (!ECC384ECDSAVerify(this->subject.uniqueId + 1, h, this->subject.uniqueIdProofSignature))
					return ZT_CERTIFICATE_ERROR_INVALID_UNIQUE_ID_PROOF;
			}
		} else if (this->subject.uniqueIdSize > 0) {
			return ZT_CERTIFICATE_ERROR_INVALID_UNIQUE_ID_PROOF;
		}

		for (unsigned int i = 0; i < this->subject.identityCount; ++i) {
			if (!this->subject.identities[i].identity)
				return ZT_CERTIFICATE_ERROR_MISSING_REQUIRED_FIELDS;
			if (checkSignatures) {
				if (!reinterpret_cast<const Identity *>(this->subject.identities[i].identity)->locallyValidate())
					return ZT_CERTIFICATE_ERROR_INVALID_IDENTITY;
				if (this->subject.identities[i].locator) {
					if (!reinterpret_cast<const Locator *>(this->subject.identities[i].locator)->verify(*reinterpret_cast<const Identity *>(this->subject.identities[i].identity)))
						return ZT_CERTIFICATE_ERROR_INVALID_COMPONENT_SIGNATURE;
				}
			}
		}

		for (unsigned int i = 0; i < this->subject.networkCount; ++i) {
			if (!this->subject.networks[i].id)
				return ZT_CERTIFICATE_ERROR_MISSING_REQUIRED_FIELDS;
		}

		if (this->subject.updateURLCount > 0) {
			if (!this->subject.updateURLs)
				return ZT_CERTIFICATE_ERROR_MISSING_REQUIRED_FIELDS;
			for (unsigned int i = 0; i < this->subject.updateURLCount; ++i) {
				if (!this->subject.updateURLs[i])
					return ZT_CERTIFICATE_ERROR_MISSING_REQUIRED_FIELDS;
			}
		}
	} catch (...) {}

	return ZT_CERTIFICATE_ERROR_NONE;
}

Vector< uint8_t > Certificate::createCSR(const ZT_Certificate_Subject &s, const void *uniqueId, unsigned int uniqueIdSize, const void *uniqueIdPrivate, unsigned int uniqueIdPrivateSize)
{
	ZT_Certificate_Subject sc;
	Utils::copy< sizeof(ZT_Certificate_Subject) >(&sc, &s);

	if ((uniqueId) && (uniqueIdSize > 0) && (uniqueIdPrivate) && (uniqueIdPrivateSize > 0)) {
		sc.uniqueId = reinterpret_cast<const uint8_t *>(uniqueId);
		sc.uniqueIdSize = uniqueIdSize;
	} else {
		sc.uniqueId = nullptr;
		sc.uniqueIdSize = 0;
	}

	Dictionary d;
	m_encodeSubject(sc, d, true);
	Vector< uint8_t > enc;
	d.encode(enc);

	if (sc.uniqueId) {
		uint8_t h[ZT_SHA384_DIGEST_SIZE];
		SHA384(h, enc.data(), (unsigned int)enc.size());
		enc.clear();
		if ((reinterpret_cast<const uint8_t *>(uniqueId)[0] == ZT_CERTIFICATE_UNIQUE_ID_TYPE_NIST_P_384) &&
		    (uniqueIdSize == ZT_CERTIFICATE_UNIQUE_ID_TYPE_NIST_P_384_SIZE) &&
		    (uniqueIdPrivateSize == ZT_CERTIFICATE_UNIQUE_ID_TYPE_NIST_P_384_PRIVATE_SIZE)) {
			uint8_t sig[ZT_ECC384_SIGNATURE_SIZE];
			ECC384ECDSASign(reinterpret_cast<const uint8_t *>(uniqueIdPrivate), h, sig);

			sc.uniqueIdProofSignature = sig;
			sc.uniqueIdProofSignatureSize = ZT_ECC384_SIGNATURE_SIZE;

			d.clear();
			m_encodeSubject(sc, d, false);
			d.encode(enc);
		}
	}

	return enc;
}

void Certificate::m_clear()
{
	ZT_Certificate *const sup = this;
	Utils::zero< sizeof(ZT_Certificate) >(sup);

	m_identities.clear();
	m_locators.clear();
	m_strings.clear();
	m_serials.clear();

	m_subjectIdentities.clear();
	m_subjectNetworks.clear();
	m_updateUrls.clear();
	m_subjectCertificates.clear();
	m_extendedAttributes.clear();
	m_subjectUniqueId.clear();
	m_subjectUniqueIdProofSignature.clear();
	m_signature.clear();
}

void Certificate::m_encodeSubject(const ZT_Certificate_Subject &s, Dictionary &d, bool omitUniqueIdProofSignature)
{
	char tmp[32];

	d.add("s.t", (uint64_t)s.timestamp);

	if (s.identities) {
		d.add("s.i$", (uint64_t)s.identityCount);
		for (unsigned int i = 0; i < s.identityCount; ++i) {
			if (s.identities[i].identity)
				d.addO(Dictionary::arraySubscript(tmp, sizeof(tmp), "s.i$.i", i), *reinterpret_cast<const Identity *>(s.identities[i].identity));
			if (s.identities[i].locator)
				d.addO(Dictionary::arraySubscript(tmp, sizeof(tmp), "s.i$.l", i), *reinterpret_cast<const Locator *>(s.identities[i].locator));
		}
	}

	if (s.networks) {
		d.add("s.nw$", (uint64_t)s.networkCount);
		for (unsigned int i = 0; i < s.networkCount; ++i) {
			d.add(Dictionary::arraySubscript(tmp, sizeof(tmp), "s.nw$.i", i), s.networks[i].id);
			Fingerprint fp(s.networks[i].controller);
			d.addO(Dictionary::arraySubscript(tmp, sizeof(tmp), "s.nw$.c", i), fp);
		}
	}

	if (s.certificates) {
		d.add("s.c$", (uint64_t)s.certificateCount);
		for (unsigned int i = 0; i < s.certificateCount; ++i) {
			if (s.certificates[i])
				d[Dictionary::arraySubscript(tmp, sizeof(tmp), "s.c$", i)].assign(s.certificates[i], s.certificates[i] + ZT_SHA384_DIGEST_SIZE);
		}
	}

	if (s.updateURLs) {
		d.add("s.u$", (uint64_t)s.updateURLCount);
		for (unsigned int i = 0; i < s.updateURLCount; ++i)
			d.add(Dictionary::arraySubscript(tmp, sizeof(tmp), "s.u$", i), s.updateURLs[i]);
	}

	if (s.name.country[0])
		d.add("s.n.c", s.name.country);
	if (s.name.organization[0])
		d.add("s.n.o", s.name.organization);
	if (s.name.unit[0])
		d.add("s.n.u", s.name.unit);
	if (s.name.locality[0])
		d.add("s.n.l", s.name.locality);
	if (s.name.province[0])
		d.add("s.n.p", s.name.province);
	if (s.name.streetAddress[0])
		d.add("s.n.sA", s.name.streetAddress);
	if (s.name.postalCode[0])
		d.add("s.n.pC", s.name.postalCode);
	if (s.name.commonName[0])
		d.add("s.n.cN", s.name.commonName);
	if (s.name.serialNo[0])
		d.add("s.n.sN", s.name.serialNo);
	if (s.name.email[0])
		d.add("s.n.e", s.name.email);
	if (s.name.url[0])
		d.add("s.n.ur", s.name.url);
	if (s.name.host[0])
		d.add("s.n.h", s.name.host);

	if ((s.uniqueId) && (s.uniqueIdSize > 0))
		d["s.uI"].assign(s.uniqueId, s.uniqueId + s.uniqueIdSize);
	if ((!omitUniqueIdProofSignature) && (s.uniqueIdProofSignature) && (s.uniqueIdProofSignatureSize > 0))
		d["s.uS"].assign(s.uniqueIdProofSignature, s.uniqueIdProofSignature + s.uniqueIdProofSignatureSize);
}

} // namespace ZeroTier
