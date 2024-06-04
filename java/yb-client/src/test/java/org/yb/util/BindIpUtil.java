/*
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 */

package org.yb.util;

import java.io.IOException;
import java.net.InetAddress;
import java.net.InterfaceAddress;
import java.net.NetworkInterface;
import java.util.*;

public class BindIpUtil {

  public static List<String> getLoopbackIPs() throws IOException {
    Enumeration<NetworkInterface> nets = NetworkInterface.getNetworkInterfaces();
    List<String> loopbackIps = new ArrayList<>();
    for (NetworkInterface netint : Collections.list(nets)) {
      for (InterfaceAddress address : netint.getInterfaceAddresses()) {
        InetAddress inetAddress = address.getAddress();
        if (inetAddress.getHostAddress().startsWith("127.")) {
          loopbackIps.add(inetAddress.getHostAddress());
        }
      }
    }
    return loopbackIps;
  }

}
