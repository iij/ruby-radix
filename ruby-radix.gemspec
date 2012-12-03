Gem::Specification.new do |s|
  s.name        = "ruby-radix"
  s.version     = "0.0.3"
  s.authors     = ["sogabe"]
  s.email       = ["sogabe@iij.ad.jp"]
  s.homepage    = "https://github.com/iij/ruby-radix"
  s.summary     = "Radix tree data structure for the storage"
  s.description = "ruby-radix is an implementation of a radix tree data structure for the storage and retrieval of IPv4 and IPv6 network prefixes."
  s.extensions  = ["ext/extconf.rb"]


  s.files         = `cat Manifest.txt`.split("\n")
  s.test_files    = `git ls-files -- {test,spec,features}/*`.split("\n")
  s.executables   = `git ls-files -- bin/*`.split("\n").map{ |f| File.basename(f) }
  s.require_paths = ["lib"]
end
