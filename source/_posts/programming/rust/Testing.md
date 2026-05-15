---
title: Rust Testing        
data: 2026-04-22 15:55:11
tags: [rust]             
categories: [unit test]              
description: Testing  
top_img: /image/jizi.png 
cover: /image/动漫少女.jpg   
---

# Testing Patterns and Strategies

| 测试类型                          | 描述                                                                 | 示例                                                                                     |
|-----------------------------------|----------------------------------------------------------------------|------------------------------------------------------------------------------------------|
| **`#[should_panic]`**             | 测试预期失败，验证代码在特定条件下会触发 panic。                      | `#[should_panic(expected = "index out of bounds")]`                                      |
| **`#[ignore]`**                   | 标记缓慢或依赖硬件的测试，默认跳过，需手动运行。                      | `#[ignore = "requires GPU hardware"]`                                                   |
| **返回 `Result` 的测试**          | 使用 `Result` 替代 `unwrap`，更清晰地处理错误。                      | `fn test_config_parsing() -> Result<(), Box<dyn std::error::Error>> { ... }`            |
| **测试夹具 (Fixtures)**           | 使用构建器模式和 `Drop` 自动清理测试资源。                            | `struct TestFixture { temp_dir: PathBuf, ... }`                                         |
| **模拟特征 (Mocking Traits)**     | 为硬件接口或外部依赖创建模拟实现，便于测试。                          | `trait IpmiTransport { fn send_command(&self, ...) -> Result<Vec<u8>, String>; }`       |
| **基于属性的测试 (proptest)**     | 测试函数在随机输入下是否满足特定属性，而非测试特定值。                | `proptest! { #[test] fn roundtrip_sensor_id(id in 0u32..10000) { ... } }`               |

---


## #[should_panic] — Testing Expected Failures  — 测试预期失败

```rs
// Test that certain conditions cause panics (like C's assert failures)
#[test]
#[should_panic(expected = "index out of bounds")]
fn test_bounds_check() {
    let v = vec![1, 2, 3];
    let _ = v[10];  // Should panic
}

#[test]
#[should_panic(expected = "temperature exceeds safe limit")]
fn test_thermal_shutdown() {
    fn check_temperature(celsius: f64) {
        if celsius > 105.0 {
            panic!("temperature exceeds safe limit: {celsius}°C");
        }
    }
    check_temperature(110.0);
}
```

## #[ignore] — Slow or Hardware-Dependent Tests  — 缓慢或依赖硬件的测试

```rs
// Mark tests that require special conditions (like C's #ifdef HARDWARE_TEST)
#[test]
#[ignore = "requires GPU hardware"]
fn test_gpu_ecc_scrub() {
    // This test only runs on machines with GPUs
    // Run with: cargo test -- --ignored
    // Run with: cargo test -- --include-ignored  (runs ALL tests)
}
```

## Result-Returning Tests (replacing unwrap chains)

```rs
// Instead of many unwrap() calls that hide the actual failure:
#[test]
fn test_config_parsing() -> Result<(), Box<dyn std::error::Error>> {
    let json = r#"{"hostname": "node-01", "port": 8080}"#;
    let config: ServerConfig = serde_json::from_str(json)?;  // ? instead of unwrap()
    assert_eq!(config.hostname, "node-01");
    assert_eq!(config.port, 8080);
    Ok(())  // Test passes if we reach here without error
}
```

## Test Fixtures with Builder Functions

使用构建器功能测试夹具  
使用辅助函数和 Drop  

```rs
struct TestFixture {
    temp_dir: std::path::PathBuf,
    config: Config,
}

impl TestFixture {
    fn new() -> Self {
        let temp_dir = std::env::temp_dir().join(format!("test_{}", std::process::id()));
        std::fs::create_dir_all(&temp_dir).unwrap();
        let config = Config {
            log_dir: temp_dir.clone(),
            max_retries: 3,
            ..Default::default()
        };
        Self { temp_dir, config }
    }
}

impl Drop for TestFixture {
    fn drop(&mut self) {
        // Automatic cleanup — like C's tearDown() but can't be forgotten
        let _ = std::fs::remove_dir_all(&self.temp_dir);
    }
}

#[test]
fn test_with_fixture() {
    let fixture = TestFixture::new();
    // Use fixture.config, fixture.temp_dir...
    assert!(fixture.temp_dir.exists());
    // fixture is automatically dropped here → cleanup runs
}
```

# Mocking Traits for Hardware Interfaces

硬件接口的模拟特征  

```rs
// Production trait for IPMI communication
trait IpmiTransport {
    fn send_command(&self, cmd: u8, data: &[u8]) -> Result<Vec<u8>, String>;
}

// Real implementation (used in production)
struct RealIpmi { /* BMC connection details */ }
impl IpmiTransport for RealIpmi {
    fn send_command(&self, cmd: u8, data: &[u8]) -> Result<Vec<u8>, String> {
        // Actually talks to BMC hardware
        todo!("Real IPMI call")
    }
}

// Mock implementation (used in tests)
struct MockIpmi {
    responses: std::collections::HashMap<u8, Vec<u8>>,
}
impl IpmiTransport for MockIpmi {
    fn send_command(&self, cmd: u8, _data: &[u8]) -> Result<Vec<u8>, String> {
        self.responses.get(&cmd)
            .cloned()
            .ok_or_else(|| format!("No mock response for cmd 0x{cmd:02x}"))
    }
}

// Generic function that works with both real and mock
fn read_sensor_temperature(transport: &dyn IpmiTransport) -> Result<f64, String> {
    let response = transport.send_command(0x2D, &[])?;
    if response.len() < 2 {
        return Err("Response too short".into());
    }
    Ok(response[0] as f64 + (response[1] as f64 / 256.0))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_temperature_reading() {
        let mut mock = MockIpmi { responses: std::collections::HashMap::new() };
        mock.responses.insert(0x2D, vec![72, 128]); // 72.5°C

        let temp = read_sensor_temperature(&mock).unwrap();
        assert!((temp - 72.5).abs() < 0.01);
    }

    #[test]
    fn test_short_response() {
        let mock = MockIpmi { responses: std::collections::HashMap::new() };
        // No response configured → error
        assert!(read_sensor_temperature(&mock).is_err());
    }
}
```

# Property-Based Testing with proptest  

使用 proptest 进行基于属性的测试  

测试必须始终保持的属性，而不是测试特定值：  

```rs
// Cargo.toml: [dev-dependencies] proptest = "1"
use proptest::prelude::*;

fn parse_sensor_id(s: &str) -> Option<u32> {
    s.strip_prefix("sensor_")?.parse().ok()
}

fn format_sensor_id(id: u32) -> String {
    format!("sensor_{id}")
}

proptest! {
    #[test]
    fn roundtrip_sensor_id(id in 0u32..10000) {
        // Property: format then parse should give back the original
        let formatted = format_sensor_id(id);
        let parsed = parse_sensor_id(&formatted);
        prop_assert_eq!(parsed, Some(id));
    }

    #[test]
    fn parse_rejects_garbage(s in "[^s].*") {
        // Property: strings not starting with 's' should never parse
        let result = parse_sensor_id(&s);
        prop_assert!(result.is_none());
    }
}
```

# Test attributes beyond #[test]

```rs
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn basic_pass() {
        assert_eq!(2 + 2, 4);
    }

    // Expect a panic — equivalent to GTest's EXPECT_DEATH
    #[test]
    #[should_panic]
    fn out_of_bounds_panics() {
        let v = vec![1, 2, 3];
        let _ = v[10]; // Panics — test passes
    }

    // Expect a panic with a specific message substring
    #[test]
    #[should_panic(expected = "index out of bounds")]
    fn specific_panic_message() {
        let v = vec![1, 2, 3];
        let _ = v[10];
    }

    // Tests that return Result<(), E> — use ? instead of unwrap()
    #[test]
    fn test_with_result() -> Result<(), String> {
        let value: u32 = "42".parse().map_err(|e| format!("{e}"))?;
        assert_eq!(value, 42);
        Ok(())
    }

    // Ignore slow tests by default — run with `cargo test -- --ignored`
    #[test]
    #[ignore]
    fn slow_integration_test() {
        std::thread::sleep(std::time::Duration::from_secs(10));
    }
}
```

```bash
cargo test                          # Run all non-ignored tests
cargo test -- --ignored             # Run only ignored tests
cargo test -- --include-ignored     # Run ALL tests including ignored
cargo test test_name                # Run tests matching a name pattern
cargo test -- --nocapture           # Show println! output during tests
cargo test -- --test-threads=1      # Run tests serially (for shared state)
```

