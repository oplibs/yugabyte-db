package redis.clients.jedis.tests.commands;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertThat;
import static org.yb.AssertionWrappers.assertTrue;

import java.util.ArrayList;
import java.util.List;

import org.hamcrest.CoreMatchers;
import org.hamcrest.Matcher;
import org.junit.Test;
import org.junit.Ignore;
import redis.clients.jedis.BinaryJedis;
import redis.clients.jedis.Jedis;
import redis.clients.jedis.exceptions.JedisConnectionException;
import redis.clients.jedis.exceptions.JedisDataException;
import redis.clients.jedis.exceptions.JedisNoScriptException;
import redis.clients.jedis.tests.utils.ClientKillerUtil;
import redis.clients.util.SafeEncoder;

import org.junit.runner.RunWith;


import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class ScriptingCommandsTest extends JedisCommandTestBase {

  @SuppressWarnings("unchecked")
  @Test
  @Ignore public void evalMultiBulk() {
    String script = "return {KEYS[1],KEYS[2],ARGV[1],ARGV[2],ARGV[3]}";
    List<String> keys = new ArrayList<String>();
    keys.add("key1");
    keys.add("key2");

    List<String> args = new ArrayList<String>();
    args.add("first");
    args.add("second");
    args.add("third");

    List<String> response = (List<String>) jedis.eval(script, keys, args);

    assertEquals(5, response.size());
    assertEquals("key1", response.get(0));
    assertEquals("key2", response.get(1));
    assertEquals("first", response.get(2));
    assertEquals("second", response.get(3));
    assertEquals("third", response.get(4));
  }

  @SuppressWarnings("unchecked")
  @Test
  @Ignore public void evalMultiBulkWithBinaryJedis() {
    String script = "return {KEYS[1],KEYS[2],ARGV[1],ARGV[2],ARGV[3]}";
    List<byte[]> keys = new ArrayList<byte[]>();
    keys.add("key1".getBytes());
    keys.add("key2".getBytes());

    List<byte[]> args = new ArrayList<byte[]>();
    args.add("first".getBytes());
    args.add("second".getBytes());
    args.add("third".getBytes());

    BinaryJedis binaryJedis =
        new BinaryJedis(hnp.getHost(), hnp.getPort(), 500);
    binaryJedis.connect();
    binaryJedis.auth("foobared");

    List<byte[]> responses =
        (List<byte[]>)binaryJedis.eval(script.getBytes(), keys, args);
    assertEquals(5, responses.size());
    assertEquals("key1", new String(responses.get(0)));
    assertEquals("key2", new String(responses.get(1)));
    assertEquals("first", new String(responses.get(2)));
    assertEquals("second", new String(responses.get(3)));
    assertEquals("third", new String(responses.get(4)));

    binaryJedis.close();
  }

  @Test
  @Ignore public void evalBulk() {
    String script = "return KEYS[1]";
    List<String> keys = new ArrayList<String>();
    keys.add("key1");

    List<String> args = new ArrayList<String>();
    args.add("first");

    String response = (String) jedis.eval(script, keys, args);

    assertEquals("key1", response);
  }

  @Test
  @Ignore public void evalInt() {
    String script = "return 2";
    List<String> keys = new ArrayList<String>();
    keys.add("key1");

    Long response = (Long) jedis.eval(script, keys, new ArrayList<String>());

    assertEquals(new Long(2), response);
  }

  @SuppressWarnings("unchecked")
  @Test
  @Ignore public void evalNestedLists() {
    String script = "return { {KEYS[1]} , {2} }";
    List<?> results = (List<?>) jedis.eval(script, 1, "key1");

    assertThat((List<String>) results.get(0), listWithItem("key1"));
    assertThat((List<Long>) results.get(1), listWithItem(2L));
  }

  @Test
  @Ignore public void evalNoArgs() {
    String script = "return KEYS[1]";
    List<String> keys = new ArrayList<String>();
    keys.add("key1");
    String response = (String) jedis.eval(script, keys, new ArrayList<String>());

    assertEquals("key1", response);
  }

  @Test
  @Ignore public void evalsha() {
    jedis.set("foo", "bar");
    jedis.eval("return redis.call('get','foo')");
    String result = (String) jedis.evalsha("6b1bf486c81ceb7edf3c093f4c48582e38c0e791");

    assertEquals("bar", result);
  }

  @Test(expected = JedisNoScriptException.class)
  @Ignore public void evalshaShaNotFound() {
    jedis.evalsha("ffffffffffffffffffffffffffffffffffffffff");
  }

  @Test
  @Ignore public void scriptFlush() {
    jedis.set("foo", "bar");
    jedis.eval("return redis.call('get','foo')");
    jedis.scriptFlush();
    assertFalse(jedis.scriptExists("6b1bf486c81ceb7edf3c093f4c48582e38c0e791"));
  }

  @Test
  @Ignore public void scriptExists() {
    jedis.scriptLoad("return redis.call('get','foo')");
    List<Boolean> exists =
        jedis.scriptExists("ffffffffffffffffffffffffffffffffffffffff",
                           "6b1bf486c81ceb7edf3c093f4c48582e38c0e791");
    assertFalse(exists.get(0));
    assertTrue(exists.get(1));
  }

  @Test
  @Ignore public void scriptExistsBinary() {
    jedis.scriptLoad(SafeEncoder.encode("return redis.call('get','foo')"));
    List<Long> exists = jedis.scriptExists(
        SafeEncoder.encode("ffffffffffffffffffffffffffffffffffffffff"),
        SafeEncoder.encode("6b1bf486c81ceb7edf3c093f4c48582e38c0e791"));
    assertEquals(new Long(0), exists.get(0));
    assertEquals(new Long(1), exists.get(1));
  }

  @Test
  @Ignore public void scriptLoad() {
    jedis.scriptLoad("return redis.call('get','foo')");
    assertTrue(jedis.scriptExists("6b1bf486c81ceb7edf3c093f4c48582e38c0e791"));
  }

  @Test
  @Ignore public void scriptLoadBinary() {
    jedis.scriptLoad(SafeEncoder.encode("return redis.call('get','foo')"));
    byte[][] scripts = new byte[1][];
    scripts[0] = SafeEncoder.encode("6b1bf486c81ceb7edf3c093f4c48582e38c0e791");
    List<Long> exists = jedis.scriptExists(scripts);
    assertEquals(new Long(1), exists.get(0));
  }

  @Test
  @Ignore public void scriptKill() {
    try {
      jedis.scriptKill();
    } catch (JedisDataException e) {
      assertTrue(e.getMessage().contains("No scripts in execution right now."));
    }
  }

  @Test
  @Ignore public void scriptEvalReturnNullValues() {
    jedis.del("key1");
    jedis.del("key2");

    String script =
        "return {redis.call('hget',KEYS[1],ARGV[1]),redis.call('hget',KEYS[2],ARGV[2])}";
    List<String> results =
        (List<String>)jedis.eval(script, 2, "key1", "key2", "1", "2");
    assertEquals(2, results.size());
    assertNull(results.get(0));
    assertNull(results.get(1));
  }

  @Test
  @Ignore public void scriptEvalShaReturnNullValues() {
    jedis.del("key1");
    jedis.del("key2");

    String script =
        "return {redis.call('hget',KEYS[1],ARGV[1]),redis.call('hget',KEYS[2],ARGV[2])}";
    String sha = jedis.scriptLoad(script);
    List<String> results =
        (List<String>)jedis.evalsha(sha, 2, "key1", "key2", "1", "2");
    assertEquals(2, results.size());
    assertNull(results.get(0));
    assertNull(results.get(1));
  }

  @Test
  @Ignore public void scriptExistsWithBrokenConnection() {
    Jedis deadClient =
        new Jedis(jedis.getClient().getHost(), jedis.getClient().getPort());
    deadClient.auth("foobared");

    deadClient.clientSetname("DEAD");

    ClientKillerUtil.killClient(deadClient, "DEAD");

    // sure, script doesn't exist, but it's just for checking connection
    try {
      deadClient.scriptExists("abcdefg");
    } catch (JedisConnectionException e) {
      // ignore it
    }

    assertEquals(true, deadClient.getClient().isBroken());

    deadClient.close();
  }

  private <T> Matcher<Iterable<? super T>> listWithItem(T expected) {
    return CoreMatchers.<T> hasItem(equalTo(expected));
  }
}
