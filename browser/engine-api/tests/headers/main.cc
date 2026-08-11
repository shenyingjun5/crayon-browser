int HeaderAdapterCompiles();
int HeaderBrowserEngineCompiles();
int HeaderEventSinkCompiles();
int HeaderIdsCompile();
int HeaderResultCompiles();
int HeaderTypesCompile();

int main() {
  return HeaderAdapterCompiles() + HeaderBrowserEngineCompiles() +
         HeaderEventSinkCompiles() + HeaderIdsCompile() +
         HeaderResultCompiles() + HeaderTypesCompile();
}
