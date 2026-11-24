/* SPDX-License-Identifier: MIT */
/* Declarations for Finch's open implementation beyond the older PD umbrella. */
#ifndef SF_FINCH_SECURITY_EXTRA_H
#define SF_FINCH_SECURITY_EXTRA_H
#include <Security/SecAccess.h>
#include <Security/SecACL.h>
#include <Security/SecPolicySearch.h>
#include <Security/SecKeychainSearch.h>
#include <Security/SecIdentitySearch.h>
#include <Security/SecCertificateOIDs.h>
CFDataRef SecCertificateCopyNormalizedIssuerSequence(SecCertificateRef);
CFDataRef SecCertificateCopyNormalizedSubjectSequence(SecCertificateRef);
CFArrayRef SecTrustCopyCertificateChain(SecTrustRef);
#endif
