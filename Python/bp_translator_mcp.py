#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
bp_translator_mcp.py

MCP (Model Context Protocol) integration for BlueprintBuster translator.
Automatically queries LLM for resolution of unsupported Blueprint nodes.

Supported queries:
  - Macro expansion (ForLoop, WhileLoop, IsValid, custom macros)
  - Delegate binding generation
  - Latent node conversion (Delay, MoveComponentTo, etc)
  - Dynamic cast simplification
  - Timeline → UCurve conversion
"""

import json
import sys
import hashlib
from typing import Optional, Dict, Any, List
from dataclasses import dataclass
from enum import Enum
import logging

# Configure logging
logging.basicConfig(level=logging.INFO, format='[%(levelname)s] %(message)s')
logger = logging.getLogger(__name__)


# ─── MCP Client ─────────────────────────────────────────────────────────────

class MCPNodeResolverClient:
	"""
	MCP (Model Context Protocol) client for querying LLM about unsupported nodes.
	
	Handles macro expansion, cast simplification, latent nodes conversion, etc.
	
	Integration with Claude/LLM:
	  - Sends node description with context
	  - Receives generated C++ code
	  - Caches results for performance
	"""
	
	def __init__(self, mcp_server_url: Optional[str] = None, enable_cache: bool = True):
		"""
		Initialize MCP client.
		
		Args:
			mcp_server_url: MCP server endpoint (e.g http://localhost:8000)
			              If None, MCP is disabled
			enable_cache: Cache query results to reduce redundant calls
		"""
		self.server_url = mcp_server_url or "http://localhost:8000"
		self.enabled = mcp_server_url is not None
		self.request_count = 0
		self.cache = {} if enable_cache else None
		self.cache_hits = 0
	
	def query_macro_expansion(self, macro_name: str, inputs: Dict[str, str], 
							  outputs: Dict[str, str]) -> Optional[str]:
		"""
		Query MCP to expand a macro instance.
		
		Example:
			macro_name = "IsValid"
			inputs = {"Value": "UObject*"}
			outputs = {"IsValid": "bool"}
		
		Returns:
			C++ code snippet or None if expansion unavailable.
		"""
		if not self.enabled:
			return None
		
		query = {
			"task": "expand_macro",
			"macro_name": macro_name,
			"inputs": inputs,
			"outputs": outputs,
		}
		
		return self._mcp_query(query, f"expand_macro_{macro_name}")
	
	def query_delegate_binding(self, delegate_type: str, handler_name: str,
							   target_class: str) -> Optional[str]:
		"""
		Query MCP for delegate binding code generation.
		
		Example:
			delegate_type = "FSimpleDelegate"
			handler_name = "OnActorSpawned"
			target_class = "AMyActor"
		
		Returns:
			C++ AddDynamic(...) code or None.
		"""
		if not self.enabled:
			return None
		
		query = {
			"task": "generate_delegate_binding",
			"delegate_type": delegate_type,
			"handler_name": handler_name,
			"target_class": target_class,
		}
		
		return self._mcp_query(query, f"delegate_{handler_name}")
	
	def query_latent_node(self, node_type: str, parameters: Dict[str, Any]) -> Optional[str]:
		"""
		Query MCP for latent node conversion.
		
		Example:
			node_type = "Delay"
			parameters = {"Duration": 1.0}
		
		Returns:
			C++ code using FTimerHandle or FLatentActionManager.
		"""
		if not self.enabled:
			return None
		
		query = {
			"task": "expand_latent_node",
			"node_type": node_type,
			"parameters": parameters,
		}
		
		return self._mcp_query(query, f"latent_{node_type}")
	
	def query_cast_simplification(self, from_class: str, to_class: str,
								  use_interface: bool = False) -> Optional[str]:
		"""
		Query MCP for safe cast generation without Cast chains.
		
		Example:
			from_class = "AActor"
			to_class = "ACharacter"
			use_interface = False
		
		Returns:
			Cast<ACharacter>(InActor) or nullptr
		"""
		if not self.enabled:
			return None
		
		query = {
			"task": "generate_safe_cast",
			"from_class": from_class,
			"to_class": to_class,
			"use_interface": use_interface,
		}
		
		return self._mcp_query(query, f"cast_{from_class}_{to_class}")
	
	def query_timeline_conversion(self, timeline_length: float, 
								  keyframes: List[Dict[str, Any]]) -> Optional[str]:
		"""
		Query MCP for Timeline → UCurve + FTimeline conversion.
		
		Returns:
			C++ code for FTimeline initialization.
		"""
		if not self.enabled:
			return None
		
		query = {
			"task": "convert_timeline",
			"length": timeline_length,
			"keyframes": keyframes,
		}
		
		return self._mcp_query(query, f"timeline_{timeline_length}")
	
	def _mcp_query(self, query_dict: Dict[str, Any], cache_key_base: str) -> Optional[str]:
		"""Internal MCP query method with caching."""
		
		# Create cache key from query
		query_str = json.dumps(query_dict, sort_keys=True)
		cache_key = hashlib.md5(query_str.encode()).hexdigest()
		
		# Check cache
		if self.cache is not None and cache_key in self.cache:
			logger.debug(f"Cache HIT: {cache_key_base}")
			self.cache_hits += 1
			return self.cache[cache_key]
		
		# Make HTTP request to MCP server
		try:
			import requests
			
			logger.info(f"MCP Query: {query_dict.get('task')} ({cache_key_base})")
			
			response = requests.post(
				f"{self.server_url}/expand",
				json=query_dict,
				timeout=10
			)
			
			if response.status_code == 200:
				result = response.json().get("code")
				if self.cache is not None:
					self.cache[cache_key] = result
				self.request_count += 1
				logger.info(f"MCP resolved: {cache_key_base}")
				return result
			else:
				logger.warning(f"MCP query failed ({cache_key_base}): HTTP {response.status_code}")
				return None
				
		except ImportError:
			logger.error("requests library not found — install with: pip install requests")
			return None
		except requests.exceptions.RequestException as e:
			logger.warning(f"MCP connection failed ({cache_key_base}): {e}")
			return None
		except Exception as e:
			logger.error(f"MCP error ({cache_key_base}): {e}")
			return None
	
	def print_stats(self):
		"""Print cache and query statistics."""
		logger.info(f"MCP Statistics:")
		logger.info(f"  Total queries: {self.request_count}")
		if self.cache is not None:
			logger.info(f"  Cache hits: {self.cache_hits}")
			logger.info(f"  Cache size: {len(self.cache)} entries")


# ─── Node Resolver with MCP Integration ─────────────────────────────────────

class MCPNodeResolver:
	"""
	Wrapper around translator's node emission to resolve Unsupported nodes
	via MCP before emitting TODO stubs.
	"""
	
	def __init__(self, mcp_client: MCPNodeResolverClient):
		self.mcp = mcp_client
		self.fallback_count = 0
	
	def resolve_unsupported_node(self, node: Any, context: Dict[str, Any]) -> Optional[str]:
		"""
		Attempt to resolve unsupported node via MCP.
		
		Returns:
			Generated C++ code or None (fallback to TODO).
		"""
		
		if not self.mcp.enabled:
			return None
		
		# Try MCP resolution based on node kind
		if node.kind == "MacroInstance":
			return self._resolve_macro(node, context)
		
		elif node.kind == "DelayNode":
			return self._resolve_latent(node, context)
		
		elif node.kind == "DynamicCast":
			return self._resolve_cast(node, context)
		
		elif node.kind == "AddDelegate":
			return self._resolve_delegate(node, context)
		
		elif node.kind == "Timeline":
			return self._resolve_timeline(node, context)
		
		else:
			return None
	
	def _resolve_macro(self, node: Any, context: Dict[str, Any]) -> Optional[str]:
		"""Resolve macro via MCP."""
		macro_name = node.label or "UnknownMacro"
		
		# Build input/output map from node
		inputs = {}
		outputs = {}
		if hasattr(node, 'args'):
			for arg in node.args:
				inputs[arg.get('name', '')] = arg.get('type', 'unknown')
		
		result = self.mcp.query_macro_expansion(macro_name, inputs, outputs)
		
		if result:
			logger.info(f"✓ Macro '{macro_name}' resolved via MCP")
			return result
		else:
			self.fallback_count += 1
			return None
	
	def _resolve_latent(self, node: Any, context: Dict[str, Any]) -> Optional[str]:
		"""Resolve latent node (Delay, MoveComponentTo, etc)."""
		node_type = node.label or "UnknownLatent"
		params = {}
		
		if hasattr(node, 'args'):
			for arg in node.args:
				params[arg.get('name', '')] = arg.get('value', '')
		
		result = self.mcp.query_latent_node(node_type, params)
		
		if result:
			logger.info(f"✓ Latent node '{node_type}' resolved via MCP")
			return result
		else:
			self.fallback_count += 1
			return None
	
	def _resolve_cast(self, node: Any, context: Dict[str, Any]) -> Optional[str]:
		"""Resolve dynamic cast to safe C++ code."""
		from_class = node.target_class or "AActor"
		to_class = node.target_class_name or "ACharacter"
		
		result = self.mcp.query_cast_simplification(from_class, to_class)
		
		if result:
			logger.info(f"✓ Cast '{from_class}' → '{to_class}' resolved via MCP")
			return result
		else:
			self.fallback_count += 1
			return None
	
	def _resolve_delegate(self, node: Any, context: Dict[str, Any]) -> Optional[str]:
		"""Resolve delegate binding."""
		delegate_name = node.delegate or "UnknownDelegate"
		handler_name = node.handler or "OnEvent"
		target_class = context.get('class_name', 'AActor')
		
		result = self.mcp.query_delegate_binding(delegate_name, handler_name, target_class)
		
		if result:
			logger.info(f"✓ Delegate binding '{handler_name}' resolved via MCP")
			return result
		else:
			self.fallback_count += 1
			return None
	
	def _resolve_timeline(self, node: Any, context: Dict[str, Any]) -> Optional[str]:
		"""Resolve Timeline node to UCurve + FTimeline."""
		timeline_length = getattr(node, 'timeline_length', 1.0)
		keyframes = getattr(node, 'keyframes', [])
		
		result = self.mcp.query_timeline_conversion(timeline_length, keyframes)
		
		if result:
			logger.info(f"✓ Timeline converted via MCP")
			return result
		else:
			self.fallback_count += 1
			return None
	
	def print_stats(self):
		"""Print resolution statistics."""
		logger.info(f"Node Resolution Statistics:")
		logger.info(f"  Fallbacks: {self.fallback_count}")
		if self.mcp.enabled:
			self.mcp.print_stats()


# ─── Integration Example ─────────────────────────────────────────────────────

def emit_node_chain_with_mcp(root: Any, indent: int, class_name: str,
							  mcp_resolver: Optional[MCPNodeResolver] = None) -> List[str]:
	"""
	Enhanced node chain emitter with MCP fallback.
	
	Signature compatible with original _emit_node_chain() but with MCP support.
	"""
	pad = "    " * indent
	out = []
	
	def emit_node(node: Any, depth: int) -> None:
		local_pad = "    " * depth
		
		# Try MCP resolution for unsupported nodes
		if node.kind == "Unsupported" and mcp_resolver:
			context = {
				'class_name': class_name,
				'indent': depth,
			}
			mcp_result = mcp_resolver.resolve_unsupported_node(node, context)
			
			if mcp_result:
				# MCP provided code — use it instead of TODO
				for line in mcp_result.split('\n'):
					if line.strip():
						out.append(f"{local_pad}{line}")
				return
		
		# Original node emission logic
		if node.kind == "CallFunction":
			arg_exprs = [a.get("expr", "") for a in node.args] if hasattr(node, 'args') else []
			args = ", ".join(arg_exprs)
			expr = f"{node.target_expr}->{node.function_name}({args})"
			out.append(f"{local_pad}{expr};")
		
		elif node.kind == "Branch":
			out.append(f"{local_pad}if ({node.condition})")
			out.append(f"{local_pad}{{")
			if hasattr(node, 'branch_true'):
				for child in node.branch_true:
					emit_node(child, depth + 1)
			out.append(f"{local_pad}}}")
			if hasattr(node, 'branch_false') and node.branch_false:
				out.append(f"{local_pad}else")
				out.append(f"{local_pad}{{")
				for child in node.branch_false:
					emit_node(child, depth + 1)
				out.append(f"{local_pad}}}")
		
		elif node.kind == "Unsupported":
			# Original: TODO stub (fallback after MCP attempt)
			reason = node.unsupported if hasattr(node, 'unsupported') else "unknown"
			out.append(f"{local_pad}// TODO: {node.kind} ({node.label}) - {reason}")
		
		else:
			out.append(f"{local_pad}// {node.kind}: {node.label}")
		
		# Recurse to next nodes
		if hasattr(node, 'next'):
			for child in node.next:
				emit_node(child, depth)
	
	emit_node(root, indent)
	return out if out else [f"{pad}// (empty)"]


# ─── CLI Integration ────────────────────────────────────────────────────────

def main_with_mcp_support():
	"""
	Example: Using bp_translator with MCP support.
	
	Usage:
	    python bp_translator_mcp.py dump.json -o ./out \\
	        --mcp-enabled --mcp-server http://localhost:8000
	"""
	import argparse
	from pathlib import Path
	
	ap = argparse.ArgumentParser(
		description="BlueprintBuster translator with MCP support"
	)
	ap.add_argument("dump", type=Path, help="Dump JSON file")
	ap.add_argument("-o", "--output", type=Path, default=Path.cwd(),
					help="Output directory")
	ap.add_argument("--module-api", default="LADOGA_API",
					help="Module export macro")
	ap.add_argument("--mcp-server", type=str, default=None,
					help="MCP server URL (e.g http://localhost:8000)")
	ap.add_argument("--mcp-enabled", action="store_true", 
					help="Enable MCP resolution for unsupported nodes")
	
	args = ap.parse_args()
	
	# Initialize MCP if requested
	mcp_client = None
	mcp_resolver = None
	
	if args.mcp_enabled and args.mcp_server:
		mcp_client = MCPNodeResolverClient(args.mcp_server)
		mcp_resolver = MCPNodeResolver(mcp_client)
		logger.info(f"MCP enabled: {args.mcp_server}")
	elif args.mcp_enabled and not args.mcp_server:
		logger.warning("--mcp-enabled specified but no --mcp-server provided")
	
	# Normal translation would happen here...
	logger.info(f"Translating: {args.dump}")
	
	# Print statistics
	if mcp_resolver:
		mcp_resolver.print_stats()


if __name__ == "__main__":
	main_with_mcp_support()
