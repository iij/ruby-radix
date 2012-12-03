require "bundler/gem_tasks"
require "rake/testtask"

task :manifest do
  File.open("Manifest.txt", "w") do |f|
    f.puts `git ls-files`.split("\n")
  end
end

Rake::TestTask.new

task :test => ['test:unit']
namespace :test do
  Rake::TestTask.new(:unit) do |t|
    t.libs << 'lib'
    t.pattern = 'test/*_test.rb'
    t.verbose = true
  end
end
