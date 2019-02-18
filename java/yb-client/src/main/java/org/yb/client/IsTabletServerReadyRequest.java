// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.client;

import org.jboss.netty.buffer.ChannelBuffer;
import org.yb.annotations.InterfaceAudience;
import org.yb.tserver.Tserver;
import org.yb.util.Pair;

import com.google.protobuf.Message;

@InterfaceAudience.Public
class IsTabletServerReadyRequest extends YRpc<IsTabletServerReadyResponse> {
  public IsTabletServerReadyRequest() {
    super(null);
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final Tserver.IsTabletServerReadyRequestPB.Builder builder =
        Tserver.IsTabletServerReadyRequestPB.newBuilder();
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return TABLET_SERVER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "IsTabletServerReady";
  }

  @Override
  Pair<IsTabletServerReadyResponse, Object> deserialize(
      CallResponse callResponse, String uuid) throws Exception {
    final Tserver.IsTabletServerReadyResponsePB.Builder respBuilder =
        Tserver.IsTabletServerReadyResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasError = respBuilder.hasError();
    IsTabletServerReadyResponse response =
        new IsTabletServerReadyResponse(deadlineTracker.getElapsedMillis(), uuid,
                                        hasError ? respBuilder.getErrorBuilder().build() : null,
                                        respBuilder.getNumTabletsNotRunning());
    return new Pair<IsTabletServerReadyResponse, Object>(response,
                                                         hasError ? respBuilder.getError() : null);
  }
}
