package redis.clients.jedis.tests;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.fail;

import java.io.IOException;
import java.net.URI;
import java.net.URISyntaxException;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.junit.Test;
import org.junit.Ignore;

import redis.clients.jedis.BinaryJedis;
import redis.clients.jedis.Jedis;
import redis.clients.jedis.JedisShardInfo;
import redis.clients.jedis.Protocol;
import redis.clients.jedis.exceptions.InvalidURIException;
import redis.clients.jedis.exceptions.JedisConnectionException;
import redis.clients.jedis.exceptions.JedisDataException;
import redis.clients.jedis.exceptions.JedisException;
import redis.clients.jedis.tests.commands.JedisCommandTestBase;
import redis.clients.util.SafeEncoder;

import org.junit.runner.RunWith;


import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class JedisTest extends JedisCommandTestBase {
  @Test
  public void useWithoutConnecting() {
    Jedis jedis = new Jedis("localhost");
    jedis.auth("foobared");
    jedis.dbSize();
  }

  @Test
  public void checkBinaryData() {
    byte[] bigdata = new byte[1777];
    for (int b = 0; b < bigdata.length; b++) {
      bigdata[b] = (byte) ((byte) b % 255);
    }
    Map<String, String> hash = new HashMap<String, String>();
    hash.put("data", SafeEncoder.encode(bigdata));

    String status = jedis.hmset("foo", hash);
    assertEquals("OK", status);
    assertEquals(hash, jedis.hgetAll("foo"));
  }

  @Test
  public void connectWithShardInfo() {
    JedisShardInfo shardInfo =
        new JedisShardInfo("localhost", Protocol.DEFAULT_PORT);
    shardInfo.setPassword("foobared");
    Jedis jedis = new Jedis(shardInfo);
    jedis.get("foo");
  }

  @Test(expected = JedisConnectionException.class)
  public void timeoutConnection() throws Exception {
    jedis = new Jedis("localhost", 6379, 15000);
    jedis.auth("foobared");
    jedis.configSet("timeout", "1");
    Thread.sleep(2000);
    jedis.hmget("foobar", "foo");
  }

  @Test(expected = JedisConnectionException.class)
  public void timeoutConnectionWithURI() throws Exception {
    jedis = new Jedis(new URI("redis://:foobared@localhost:6380/2"), 15000);
    jedis.configSet("timeout", "1");
    Thread.sleep(2000);
    jedis.hmget("foobar", "foo");
  }

  @Test(expected = JedisDataException.class)
  public void failWhenSendingNullValues() {
    jedis.set("foo", null);
  }

  @Test(expected = InvalidURIException.class)
  public void shouldThrowInvalidURIExceptionForInvalidURI()
      throws URISyntaxException {
    Jedis j = new Jedis(new URI("localhost:6380"));
    j.ping();
  }

  @Test
  @Ignore public void shouldReconnectToSameDB() throws IOException {
    jedis.select(1);
    jedis.set("foo", "bar");
    jedis.getClient().getSocket().shutdownInput();
    jedis.getClient().getSocket().shutdownOutput();
    assertEquals("bar", jedis.get("foo"));
  }

  @Test
  @Ignore public void startWithUrlString() {
    Jedis j = new Jedis("localhost", 6380);
    j.auth("foobared");
    j.select(2);
    j.set("foo", "bar");
    Jedis jedis = new Jedis("redis://:foobared@localhost:6380/2");
    assertEquals("PONG", jedis.ping());
    assertEquals("bar", jedis.get("foo"));
  }

  @Test
  @Ignore public void startWithUrl() throws URISyntaxException {
    Jedis j = new Jedis("localhost", 6380);
    j.auth("foobared");
    j.select(2);
    j.set("foo", "bar");
    Jedis jedis = new Jedis(new URI("redis://:foobared@localhost:6380/2"));
    assertEquals("PONG", jedis.ping());
    assertEquals("bar", jedis.get("foo"));
  }

  @Test
  public void shouldNotUpdateDbIndexIfSelectFails() throws URISyntaxException {
    String currentDb = jedis.getDB();
    try {
      int invalidDb = -1;
      jedis.select(invalidDb);

      fail("Should throw an exception if tried to select invalid db");
    } catch (JedisException e) {
      assertEquals(currentDb, jedis.getDB());
    }
  }

  @Test
  @Ignore public void allowUrlWithNoDBAndNoPassword() {
    Jedis jedis = new Jedis("redis://localhost:6380");
    jedis.auth("foobared");
    assertEquals(jedis.getClient().getHost(), "localhost");
    assertEquals(jedis.getClient().getPort(), 6380);
    assertEquals(jedis.getDB(), (Long) 0L);

    jedis = new Jedis("redis://localhost:6380/");
    jedis.auth("foobared");
    assertEquals(jedis.getClient().getHost(), "localhost");
    assertEquals(jedis.getClient().getPort(), 6380);
    assertEquals(jedis.getDB(), (Long) 0L);
  }

  @Test
  public void checkCloseable() {
    jedis.close();
    BinaryJedis bj = new BinaryJedis("localhost");
    bj.connect();
    bj.close();
  }

  @Test
  @Ignore public void testBitfield() {
    Jedis jedis = new Jedis("redis://localhost:6380");
    jedis.auth("foobared");
    jedis.del("mykey");
    try {
      List<Long> responses =
          jedis.bitfield("mykey", "INCRBY", "i5", "100", "1", "GET", "u4", "0");
      assertEquals(1l, responses.get(0).longValue());
      assertEquals(0l, responses.get(1).longValue());
    } finally {
      jedis.del("mykey");
    }
  }


  @Test
  /**
   * Binary Jedis tests should be in their own class
   */
  public void testBinaryBitfield() {
    jedis.close();
    BinaryJedis binaryJedis = new BinaryJedis("localhost");
    binaryJedis.auth("foobared");
    binaryJedis.del("mykey".getBytes());
    try {
      List<byte[]> responses = binaryJedis.bitfield(
          "mykey".getBytes(), "INCRBY".getBytes(), "i5".getBytes(),
          "100".getBytes(), "1".getBytes(), "GET".getBytes(), "u4".getBytes(),
          "0".getBytes());
      assertEquals(1l, responses.get(0));
      assertEquals(0l, responses.get(1));
    } finally {
      binaryJedis.del("mykey".getBytes());
      binaryJedis.close();
    }
  }

}
