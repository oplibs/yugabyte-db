/**
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
 *
 */
package org.yb;

import org.junit.runner.Description;
import org.junit.runner.notification.RunNotifier;
import org.junit.runners.BlockJUnit4ClassRunner;
import org.junit.runners.model.FrameworkMethod;
import org.junit.runners.model.InitializationError;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.util.ConfForTesting;

import java.util.Collections;
import java.util.List;

public class YBTestRunner extends BlockJUnit4ClassRunner {

  private static final Logger LOG = LoggerFactory.getLogger(YBTestRunner.class);

  /**
   * Creates a BlockJUnit4ClassRunner to run {@code klass}
   *
   * @param klass
   * @throws InitializationError if the test class is malformed.
   */
  public YBTestRunner(Class<?> klass) throws InitializationError {
    super(klass);
    if (ConfForTesting.onlyCollectingTests()) {
      for (FrameworkMethod method : super.getChildren()) {
        TestUtils.reportCollectedTest(
            method.getDeclaringClass().getName(), method.getMethod().getName());
      }
    }
  }

  @Override
  protected List<FrameworkMethod> getChildren() {
    if (ConfForTesting.onlyCollectingTests()) {
      return Collections.emptyList();
    }
    return super.getChildren();
  }

  @Override
  protected void runChild(FrameworkMethod method, RunNotifier notifier) {
    LOG.info("RunChild is running for method " + method);
    try {
      super.runChild(method, notifier);
    } catch (AssertionError assertionError) {
      LOG.error("Assertion failure in test method: " + method, assertionError);
      throw assertionError;
    }
  }

}
