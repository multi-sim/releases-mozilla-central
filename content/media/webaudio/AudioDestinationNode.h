/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef AudioDestinationNode_h_
#define AudioDestinationNode_h_

#include "AudioNode.h"

namespace mozilla {
namespace dom {

class AudioContext;

/**
 * Need to have an nsWrapperCache on AudioDestinationNodes since
 * AudioContext.destination returns them.
 */
class AudioDestinationNode : public AudioNode,
                             public nsWrapperCache
{
public:
  explicit AudioDestinationNode(AudioContext* aContext);

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(AudioDestinationNode,
                                                         AudioNode)

  virtual JSObject* WrapObject(JSContext* aCx, JSObject* aScope,
                               bool* aTriedToWrap);

  virtual uint32_t NumberOfOutputs() const MOZ_FINAL MOZ_OVERRIDE
  {
    return 0;
  }

};

}
}

#endif

