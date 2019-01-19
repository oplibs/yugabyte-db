package redis.clients.jedis.tests.commands;

import static org.yb.AssertionWrappers.assertEquals;

import org.junit.Test;

import redis.clients.jedis.BinaryJedis;
import redis.clients.jedis.HostAndPort;
import redis.clients.jedis.tests.HostAndPortUtil;

import org.junit.runner.RunWith;


import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class ConnectionHandlingCommandsTest extends JedisCommandTestBase {
  protected static HostAndPort hnp = HostAndPortUtil.getRedisServers().get(0);

  @Test
  public void quit() {
    assertEquals("OK", jedis.quit());
  }

  @Test
  public void binary_quit() {
    BinaryJedis bj = new BinaryJedis(hnp.getHost());
    assertEquals("OK", bj.quit());
  }
}
