package redis.clients.jedis.tests;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.net.URI;
import java.net.URISyntaxException;
import java.util.concurrent.atomic.AtomicInteger;

import org.apache.commons.pool2.PooledObject;
import org.apache.commons.pool2.PooledObjectFactory;
import org.apache.commons.pool2.impl.DefaultPooledObject;
import org.apache.commons.pool2.impl.GenericObjectPoolConfig;

import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;

import redis.clients.jedis.HostAndPort;
import redis.clients.jedis.Jedis;
import redis.clients.jedis.JedisPool;
import redis.clients.jedis.JedisPoolConfig;
import redis.clients.jedis.Transaction;
import redis.clients.jedis.exceptions.InvalidURIException;
import redis.clients.jedis.exceptions.JedisException;

import org.junit.runner.RunWith;

import redis.clients.jedis.tests.BaseYBClassForJedis;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class JedisPoolTest extends BaseYBClassForJedis {
  private static HostAndPort hnp;

  @Before
  public void setUp() throws Exception {
    hnp =  new HostAndPort(
          miniCluster.getRedisContactPoints().get(0).getHostString(),
          miniCluster.getRedisContactPoints().get(0).getPort());
    createRedisTableForDB("1");
  }

  @Test
  public void checkConnections() {
    JedisPool pool = new JedisPool(new JedisPoolConfig(), hnp.getHost(), hnp.getPort(), 12000);
    Jedis jedis = pool.getResource();
    jedis.auth("foobared");
    jedis.set("foo", "bar");
    assertEquals("bar", jedis.get("foo"));
    pool.returnResource(jedis);
    pool.destroy();
    assertTrue(pool.isClosed());
  }

  @Test
  public void checkCloseableConnections() throws Exception {
    JedisPool pool = new JedisPool(new JedisPoolConfig(), hnp.getHost(), hnp.getPort(), 12000);
    Jedis jedis = pool.getResource();
    jedis.auth("foobared");
    jedis.set("foo", "bar");
    assertEquals("bar", jedis.get("foo"));
    pool.returnResource(jedis);
    pool.close();
    assertTrue(pool.isClosed());
  }

  @Test
  public void checkConnectionWithDefaultPort() {
    JedisPool pool = new JedisPool(new JedisPoolConfig(), hnp.getHost(), hnp.getPort());
    Jedis jedis = pool.getResource();
    jedis.auth("foobared");
    jedis.set("foo", "bar");
    assertEquals("bar", jedis.get("foo"));
    pool.returnResource(jedis);
    pool.destroy();
    assertTrue(pool.isClosed());
  }

  @Test
  public void checkJedisIsReusedWhenReturned() {

    JedisPool pool = new JedisPool(new JedisPoolConfig(), hnp.getHost(), hnp.getPort());
    Jedis jedis = pool.getResource();
    jedis.auth("foobared");
    jedis.set("foo", "0");
    pool.returnResource(jedis);

    jedis = pool.getResource();
    jedis.auth("foobared");
    jedis.incr("foo");
    pool.returnResource(jedis);
    pool.destroy();
    assertTrue(pool.isClosed());
  }

  @Test
  public void checkPoolRepairedWhenJedisIsBroken() {
    JedisPool pool = new JedisPool(new JedisPoolConfig(), hnp.getHost(), hnp.getPort());
    Jedis jedis = pool.getResource();
    jedis.auth("foobared");
    jedis.quit();
    pool.returnBrokenResource(jedis);

    jedis = pool.getResource();
    jedis.auth("foobared");
    jedis.incr("foo");
    pool.returnResource(jedis);
    pool.destroy();
    assertTrue(pool.isClosed());
  }

  @Test(expected = JedisException.class)
  public void checkPoolOverflow() {
    GenericObjectPoolConfig config = new GenericObjectPoolConfig();
    config.setMaxTotal(1);
    config.setBlockWhenExhausted(false);
    JedisPool pool = new JedisPool(config, hnp.getHost(), hnp.getPort());
    Jedis jedis = pool.getResource();
    jedis.auth("foobared");
    jedis.set("foo", "0");

    Jedis newJedis = pool.getResource();
    newJedis.auth("foobared");
    newJedis.incr("foo");
  }

  @Test
  public void securePool() {
    JedisPoolConfig config = new JedisPoolConfig();
    config.setTestOnBorrow(true);
    JedisPool pool = new JedisPool(config, hnp.getHost(), hnp.getPort(), 12000, "foobared");
    Jedis jedis = pool.getResource();
    jedis.set("foo", "bar");
    pool.returnResource(jedis);
    pool.destroy();
    assertTrue(pool.isClosed());
  }

  @Test
  public void nonDefaultDatabase() {
    JedisPool pool0 = new JedisPool(new JedisPoolConfig(), hnp.getHost(), hnp.getPort(), 12000,
        "foobared");
    Jedis jedis0 = pool0.getResource();
    jedis0.set("foo", "bar");
    assertEquals("bar", jedis0.get("foo"));
    pool0.returnResource(jedis0);
    pool0.destroy();
    assertTrue(pool0.isClosed());

    JedisPool pool1 = new JedisPool(new JedisPoolConfig(), hnp.getHost(), hnp.getPort(), 12000,
        "foobared", "1");
    Jedis jedis1 = pool1.getResource();
    assertNull(jedis1.get("foo"));
    pool1.returnResource(jedis1);
    pool1.destroy();
    assertTrue(pool1.isClosed());
  }

  @Test
  @Ignore public void startWithUrlString() {
    Jedis j = new Jedis("localhost", 6380);
    j.auth("foobared");
    j.select(2);
    j.set("foo", "bar");
    JedisPool pool = new JedisPool("redis://:foobared@localhost:6380/2");
    Jedis jedis = pool.getResource();
    assertEquals("PONG", jedis.ping());
    assertEquals("bar", jedis.get("foo"));
  }

  @Test
  @Ignore public void startWithUrl() throws URISyntaxException {
    Jedis j = new Jedis("localhost", 6380);
    j.auth("foobared");
    j.select(2);
    j.set("foo", "bar");
    JedisPool pool = new JedisPool(new URI("redis://:foobared@localhost:6380/2"));
    Jedis jedis = pool.getResource();
    assertEquals("PONG", jedis.ping());
    assertEquals("bar", jedis.get("foo"));
  }

  @Test(expected = InvalidURIException.class)
  public void shouldThrowInvalidURIExceptionForInvalidURI() throws URISyntaxException {
    JedisPool pool = new JedisPool(new URI("localhost:6380"));
  }

  @Test
  public void allowUrlWithNoDBAndNoPassword() throws URISyntaxException {
    new JedisPool("redis://localhost:6380");
    new JedisPool(new URI("redis://localhost:6380"));
  }

  @Test
  public void selectDatabaseOnActivation() {
    JedisPool pool = new JedisPool(new JedisPoolConfig(), hnp.getHost(), hnp.getPort(), 12000,
        "foobared");

    Jedis jedis0 = pool.getResource();
    assertEquals("0", jedis0.getDB());

    jedis0.select(1);
    assertEquals("1", jedis0.getDB());

    pool.returnResource(jedis0);

    Jedis jedis1 = pool.getResource();
    assertTrue("Jedis instance was not reused", jedis1 == jedis0);
    assertEquals("0", jedis1.getDB());

    pool.returnResource(jedis1);
    pool.destroy();
    assertTrue(pool.isClosed());
  }

  @Test
  @Ignore public void customClientName() {
    JedisPool pool0 = new JedisPool(new JedisPoolConfig(), hnp.getHost(), hnp.getPort(), 12000,
        "foobared", "0", "my_shiny_client_name");

    Jedis jedis = pool0.getResource();

    assertEquals("my_shiny_client_name", jedis.clientGetname());

    pool0.returnResource(jedis);
    pool0.destroy();
    assertTrue(pool0.isClosed());
  }

  @Test
  @Ignore public void returnResourceDestroysResourceOnException() {

    class CrashingJedis extends Jedis {
      @Override
      public void resetState() {
        throw new RuntimeException();
      }
    }

    final AtomicInteger destroyed = new AtomicInteger(0);

    class CrashingJedisPooledObjectFactory implements PooledObjectFactory<Jedis> {

      @Override
      public PooledObject<Jedis> makeObject() throws Exception {
        return new DefaultPooledObject<Jedis>(new CrashingJedis());
      }

      @Override
      public void destroyObject(PooledObject<Jedis> p) throws Exception {
        destroyed.incrementAndGet();
      }

      @Override
      public boolean validateObject(PooledObject<Jedis> p) {
        return true;
      }

      @Override
      public void activateObject(PooledObject<Jedis> p) throws Exception {
      }

      @Override
      public void passivateObject(PooledObject<Jedis> p) throws Exception {
      }
    }

    GenericObjectPoolConfig config = new GenericObjectPoolConfig();
    config.setMaxTotal(1);
    JedisPool pool = new JedisPool(config, hnp.getHost(), hnp.getPort(), 12000, "foobared");
    pool.initPool(config, new CrashingJedisPooledObjectFactory());
    Jedis crashingJedis = pool.getResource();

    try {
      pool.returnResource(crashingJedis);
    } catch (Exception ignored) {
    }

    assertEquals(destroyed.get(), 1);
  }

  @Test
  @Ignore public void returnResourceShouldResetState() {
    GenericObjectPoolConfig config = new GenericObjectPoolConfig();
    config.setMaxTotal(1);
    config.setBlockWhenExhausted(false);
    JedisPool pool = new JedisPool(config, hnp.getHost(), hnp.getPort(), 12000, "foobared");

    Jedis jedis = pool.getResource();
    try {
      jedis.set("hello", "jedis");
      Transaction t = jedis.multi();
      t.set("hello", "world");
    } finally {
      jedis.close();
    }

    Jedis jedis2 = pool.getResource();
    try {
      assertTrue(jedis == jedis2);
      assertEquals("jedis", jedis2.get("hello"));
    } finally {
      jedis2.close();
    }

    pool.destroy();
    assertTrue(pool.isClosed());
  }

  @Test
  public void checkResourceIsCloseable() {
    GenericObjectPoolConfig config = new GenericObjectPoolConfig();
    config.setMaxTotal(1);
    config.setBlockWhenExhausted(false);
    JedisPool pool = new JedisPool(config, hnp.getHost(), hnp.getPort(), 12000, "foobared");

    Jedis jedis = pool.getResource();
    try {
      jedis.set("hello", "jedis");
    } finally {
      jedis.close();
    }

    Jedis jedis2 = pool.getResource();
    try {
      assertEquals(jedis, jedis2);
    } finally {
      jedis2.close();
    }
  }

  @Test
  public void returnNullObjectShouldNotFail() {
    JedisPool pool = new JedisPool(new JedisPoolConfig(), hnp.getHost(), hnp.getPort(), 12000,
        "foobared", "0", "my_shiny_client_name");

    pool.returnBrokenResource(null);
    pool.returnResource(null);
    pool.returnResourceObject(null);
  }

  @Test
  public void getNumActiveIsNegativeWhenPoolIsClosed() {
    JedisPool pool = new JedisPool(new JedisPoolConfig(), hnp.getHost(), hnp.getPort(), 12000,
        "foobared", "0", "my_shiny_client_name");

    pool.destroy();
    assertTrue(pool.getNumActive() < 0);
  }

  @Test
  public void getNumActiveReturnsTheCorrectNumber() {
    JedisPool pool = new JedisPool(new JedisPoolConfig(), hnp.getHost(), hnp.getPort(), 12000);
    Jedis jedis = pool.getResource();
    jedis.auth("foobared");
    jedis.set("foo", "bar");
    assertEquals("bar", jedis.get("foo"));

    assertEquals(1, pool.getNumActive());

    Jedis jedis2 = pool.getResource();
    jedis.auth("foobared");
    jedis.set("foo", "bar");

    assertEquals(2, pool.getNumActive());

    pool.returnResource(jedis);
    assertEquals(1, pool.getNumActive());

    pool.returnResource(jedis2);

    assertEquals(0, pool.getNumActive());

    pool.destroy();
  }

  @Test
  public void testAddObject() {
    JedisPool pool = new JedisPool(new JedisPoolConfig(), hnp.getHost(), hnp.getPort(), 12000);
    pool.addObjects(1);
    assertEquals(pool.getNumIdle(), 1);
    pool.destroy();

  }

  @Test
  @Ignore public void testCloseConnectionOnMakeObject() {
    JedisPoolConfig config = new JedisPoolConfig();
    config.setTestOnBorrow(true);
    JedisPool pool = new JedisPool(new JedisPoolConfig(), hnp.getHost(), hnp.getPort(), 12000,
        "wrong pass");
    Jedis jedis = new Jedis("redis://:foobared@" + hnp.getHost() + ":" + hnp.getPort() + "/");
    int currentClientCount = getClientCount(jedis.clientList());
    try {
      pool.getResource();
      fail("Should throw exception as password is incorrect.");
    } catch (Exception e) {
      assertEquals(currentClientCount, getClientCount(jedis.clientList()));
    }

  }

  private int getClientCount(final String clientList) {
    return clientList.split("\n").length;
  }

}
