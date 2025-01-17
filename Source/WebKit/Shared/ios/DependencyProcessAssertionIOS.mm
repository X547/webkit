/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#import "config.h"
#import "DependencyProcessAssertion.h"

#if PLATFORM(IOS_FAMILY)

#import "RunningBoardServicesSPI.h"

namespace WebKit {

DependencyProcessAssertion::DependencyProcessAssertion(ProcessID targetPID, ASCIILiteral description)
{
    RBSTarget *target = [RBSTarget targetWithPid:targetPID];
    RBSDomainAttribute *domainAttribute = [RBSDomainAttribute attributeWithDomain:@"com.apple.webkit" name:@"DependentProcessLink"];
    m_assertion = adoptNS([[RBSAssertion alloc] initWithExplanation:String { description } target:target attributes:@[domainAttribute]]);
    NSError *acquisitionError = nil;
    if (![m_assertion acquireWithError:&acquisitionError])
        RELEASE_LOG_ERROR(Process, "DependencyProcessAssertion::DependencyProcessAssertion: Failed to acquire dependency process assertion '%{public}s', error: %{public}@", description.characters(), acquisitionError);
    else
        RELEASE_LOG(Process, "DependencyProcessAssertion::DependencyProcessAssertion: Successfully took a dependency process assertion '%{public}s' for target process with PID %d", description.characters(), targetPID);
}

DependencyProcessAssertion::~DependencyProcessAssertion()
{
    [m_assertion invalidate];
}

} // namespace WebKit

#endif // PLATFORM(IOS_FAMILY)
