require File.dirname(__FILE__) + '/test_helper.rb'

require 'radix'

def int2ipaddr(val)
  rstr = ""
  rstr += sprintf("%d.", (val & 0xff000000) >> 24)
  rstr += sprintf("%d.", (val & 0xff0000) >> 16)
  rstr += sprintf("%d.", (val & 0xff00) >> 8)
  rstr += sprintf("%d", val & 0xff)
  rstr
end

class TestRubyRadix < Test::Unit::TestCase

  def setup
  end

  def test_add
    r = Radix.new
    r.add("192.168.0.0", 24)
    assert r.class == Radix
  end

  def test_store
    r = Radix.new
    r["172.31.0.0"] = "world!"
    assert r["172.31.0.0/30"].msg == "world!"
  end

  def test_add_objects
    r = Radix.new
    r["192.168.0.0/24"] = ["Hello", "Radix", "Tree"]
    r["172.31.0.0/30"] = {"Hello" => 123, "Radix" => "abc", :Tree => "def"}
    assert r["192.168.0.0/24"].msg.class == Array
    assert r["172.31.0.0/30"].msg.class == Hash
    assert r["192.168.0.0/24"].msg[2] == "Tree"
    assert r["172.31.0.0/30"].msg[:Tree] = "def"
  end

  def test_search_best
    r = Radix.new
    r["10.0.0.0/8"] = "message 1"
    r["4.3.2.0/24"] = "message 2"
    assert r.search_best("10.0.0.1").msg == "message 1"
    assert r.search_best("11.0.0.1") == nil
    assert r.search_best("4.3.2.0/24").msg == "message 2"
  end

  def test_search_exact
    r = Radix.new
    r["10.0.0.0/8"] = "message 1"
    r["172.31.1.224/29"] = "message 2"
    assert r.search_exact("10.0.0.0/8").msg == "message 1"
    assert r.search_exact("10.0.0.0/24") == nil
    assert r.search_exact("172.31.1.224/29").msg == "message 2"
  end


  def test_search_prefix
    r = Radix.new
    r["10.0.0.0/8"] = "next hop of 10.0.0.0/24 is 2.2.2.2"
    node = r.search_best("10.0.1.2/32")
    assert_match(/^next hop of 10\.0\.0\.0\/24 is 2\.2\.2\.2$/, node.msg)
  end

  def test_values
    r = Radix.new
    r["10.0.0.0/8"] = "message 1"
    r["4.3.2.0/24"] = "message 2"
    ret0 = r.values[0]
    ret = ret0 + r.values[1]
    assert ret.include? "message 1"
    assert ret.include? "message 2"
  end

  def test_keys
    r = Radix.new
    r["10.0.0.0/8"] = "message 1"
    r["4.3.2.0/24"] = "message 2"
    ret1 = r.keys[0]
    ret2 = r.keys[1]
    ret = ret1 + " " + ret2
    assert ret.include? "10.0.0.0/8"
    assert ret.include? "4.3.2.0/24"
  end

  def test_length
    r = Radix.new
    r["10.0.0.0/8"] = "message 1"
    r["4.3.2.0/24"] = "message 2"
    assert r.length == 2
  end

  def test_clear
    for i in 1..100
      r = Radix.new
      r[int2ipaddr(i)] = int2ipaddr(i)
      r.clear
      assert r.length == 0
    end
  end

  def test_add10000
    r = Radix.new
    for i in 1..1000
      r[int2ipaddr(i)] = int2ipaddr(i)
    end
    assert r.length == 1000
    assert r[int2ipaddr(1000)].msg == int2ipaddr(1000)
  end

  def test_eachpair
    r = Radix.new
    r["10.0.0.0/8"] = "message 1"
    r["4.3.2.0/24"] = "message 2"
    r.each_pair do |k, v|
      if (k == "1.2.3.4/32")
        assert v == "message 1"
      end
      if (k == "4.3.2.1/32")
        assert v == "message 2"
      end
    end
  end

end
